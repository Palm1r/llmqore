// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/JsonRpcSession.hpp>

#include <LLMQore/Log.hpp>
#include <LLMQore/RpcExceptions.hpp>

#include <QFutureWatcher>
#include <QTimer>

namespace LLMQore::Rpc {

namespace {

QJsonObject withProgressToken(const QJsonObject &params, const QString &progressToken)
{
    QJsonObject out = params;
    QJsonObject meta = out.value("_meta").toObject();
    meta.insert("progressToken", progressToken);
    out.insert("_meta", meta);
    return out;
}

} // namespace

JsonRpcSession::JsonRpcSession(Transport *transport, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
{
    if (m_transport) {
        connect(
            m_transport,
            &Transport::messageReceived,
            this,
            &JsonRpcSession::onMessageReceived);
        connect(m_transport, &Transport::closed, this, &JsonRpcSession::onTransportClosed);
    }

    setNotificationHandler(
        QStringLiteral("notifications/cancelled"), [this](const QJsonObject &params) {
            const QJsonValue idVal = params.value("requestId");
            QString id;
            if (idVal.isString())
                id = idVal.toString();
            else if (idVal.isDouble())
                id = QString::number(static_cast<qint64>(idVal.toDouble()));
            else
                return;
            const QString reason = params.value("reason").toString();

            if (m_inFlightIncomingIds.contains(id))
                m_cancelledIncomingIds.insert(id);

            auto it = m_pending.find(id);
            if (it != m_pending.end()) {
                auto p = it->promise;
                if (it->timer) {
                    it->timer->stop();
                    it->timer->deleteLater();
                }
                if (!it->progressToken.isEmpty())
                    clearProgressHandler(it->progressToken);
                m_pending.erase(it);
                p->setException(std::make_exception_ptr(CancelledError(
                    reason.isEmpty() ? QStringLiteral("Cancelled by peer") : reason)));
                p->finish();
            }
        });

    setNotificationHandler(
        QStringLiteral("notifications/progress"), [this](const QJsonObject &params) {
            const QJsonValue tokVal = params.value("progressToken");
            QString token;
            if (tokVal.isString())
                token = tokVal.toString();
            else if (tokVal.isDouble())
                token = QString::number(static_cast<qint64>(tokVal.toDouble()));
            const double progress = params.value("progress").toDouble();
            const double total = params.value("total").toDouble();
            const QString message = params.value("message").toString();
            emit progressReceived(token, progress, total, message);

            auto it = m_progressHandlers.find(token);
            if (it != m_progressHandlers.end()) {
                try {
                    it.value()(progress, total, message);
                } catch (const std::exception &e) {
                    qCWarning(llmMcpLog).noquote()
                        << QString("Progress handler for %1 threw: %2").arg(token, e.what());
                }
            }
        });
}

JsonRpcSession::~JsonRpcSession()
{
    abortPending(QStringLiteral("Session destroyed"));
}

QString JsonRpcSession::allocateId()
{
    return QString::number(m_nextId.fetchAndAddRelaxed(1));
}

JsonRpcSession::CancellableRequest JsonRpcSession::sendRequestImpl(
    const QString &method,
    const QJsonObject &params,
    std::chrono::milliseconds timeout,
    bool trackProgressToken)
{
    CancellableRequest out;

    auto promise = std::make_shared<QPromise<QJsonValue>>();
    promise->start();

    if (!m_transport || !m_transport->isOpen()) {
        promise->setException(
            std::make_exception_ptr(TransportError(QStringLiteral("Transport is not open"))));
        promise->finish();
        out.future = promise->future();
        return out;
    }

    const QString id = allocateId();
    out.requestId = id;

    QTimer *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(static_cast<int>(timeout.count()));

    Pending pending{promise, timer, trackProgressToken ? id : QString()};
    m_pending.insert(id, pending);

    connect(timer, &QTimer::timeout, this, [this, id]() {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        auto p = it->promise;
        if (it->timer)
            it->timer->deleteLater();
        if (!it->progressToken.isEmpty())
            clearProgressHandler(it->progressToken);
        m_pending.erase(it);
        qCWarning(llmMcpLog).noquote() << QString("Request %1 timed out").arg(id);
        p->setException(
            std::make_exception_ptr(TimeoutError(QString("Request %1 timed out").arg(id))));
        p->finish();
    });
    timer->start();

    QJsonObject message{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
    };
    const QJsonObject outgoing
        = trackProgressToken ? withProgressToken(params, id) : params;
    if (!outgoing.isEmpty())
        message.insert("params", outgoing);

    qCDebug(llmMcpLog).noquote()
        << QString("--> request id=%1 method=%2%3")
               .arg(id,
                    method,
                    trackProgressToken ? QStringLiteral(" (cancellable)") : QString());
    m_transport->send(message);

    out.future = promise->future();
    return out;
}

QFuture<QJsonValue> JsonRpcSession::sendRequest(
    const QString &method, const QJsonObject &params, std::chrono::milliseconds timeout)
{
    return sendRequestImpl(method, params, timeout, /*trackProgressToken=*/false).future;
}

JsonRpcSession::CancellableRequest JsonRpcSession::sendCancellableRequest(
    const QString &method, const QJsonObject &params, std::chrono::milliseconds timeout)
{
    return sendRequestImpl(method, params, timeout, /*trackProgressToken=*/true);
}

void JsonRpcSession::cancelRequest(const QString &id, const QString &reason)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;

    QJsonObject params{{"requestId", id}};
    if (!reason.isEmpty())
        params.insert("reason", reason);
    sendNotification(QStringLiteral("notifications/cancelled"), params);

    auto p = it->promise;
    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    if (!it->progressToken.isEmpty())
        clearProgressHandler(it->progressToken);
    m_pending.erase(it);
    p->setException(std::make_exception_ptr(CancelledError(
        reason.isEmpty() ? QStringLiteral("Cancelled by caller") : reason)));
    p->finish();
}

void JsonRpcSession::sendNotification(const QString &method, const QJsonObject &params)
{
    if (!m_transport || !m_transport->isOpen()) {
        qCWarning(llmMcpLog).noquote()
            << QString("Dropping notification %1: transport not open").arg(method);
        return;
    }

    QJsonObject message{
        {"jsonrpc", "2.0"},
        {"method", method},
    };
    if (!params.isEmpty())
        message.insert("params", params);

    qCDebug(llmMcpLog).noquote() << QString("--> notify method=%1").arg(method);
    m_transport->send(message);
}

void JsonRpcSession::setRequestHandler(const QString &method, RequestHandler handler)
{
    if (handler)
        m_requestHandlers.insert(method, std::move(handler));
    else
        m_requestHandlers.remove(method);
}

void JsonRpcSession::setNotificationHandler(const QString &method, NotifyHandler handler)
{
    if (handler)
        m_notifyHandlers.insert(method, std::move(handler));
    else
        m_notifyHandlers.remove(method);
}

void JsonRpcSession::setProgressHandler(const QString &progressToken, ProgressHandler handler)
{
    if (handler)
        m_progressHandlers.insert(progressToken, std::move(handler));
    else
        m_progressHandlers.remove(progressToken);
}

void JsonRpcSession::clearProgressHandler(const QString &progressToken)
{
    m_progressHandlers.remove(progressToken);
}

void JsonRpcSession::sendProgress(
    const QString &progressToken, double progress, double total, const QString &message)
{
    if (progressToken.isEmpty())
        return;
    QJsonObject params{
        {"progressToken", progressToken},
        {"progress", progress},
    };
    if (total > 0.0)
        params.insert("total", total);
    if (!message.isEmpty())
        params.insert("message", message);
    sendNotification(QStringLiteral("notifications/progress"), params);
}

bool JsonRpcSession::isRequestCancelled(const QString &requestId) const
{
    return m_cancelledIncomingIds.contains(requestId);
}

void JsonRpcSession::abortPending(const QString &reason)
{
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->timer) {
            it->timer->stop();
            it->timer->deleteLater();
        }
        if (!it->progressToken.isEmpty())
            clearProgressHandler(it->progressToken);
        it->promise->setException(std::make_exception_ptr(TransportError(reason)));
        it->promise->finish();
        it = m_pending.erase(it);
    }
}

void JsonRpcSession::onMessageReceived(const QJsonObject &message)
{
    const QString jsonrpc = message.value("jsonrpc").toString();
    if (jsonrpc != QLatin1String("2.0")) {
        qCWarning(llmMcpLog).noquote()
            << QString("Dropping message with unexpected jsonrpc field: %1").arg(jsonrpc);
        emit protocolError(QStringLiteral("Invalid jsonrpc field"));
        return;
    }

    const bool hasId = message.contains("id") && !message.value("id").isNull();
    const bool hasMethod = message.contains("method");
    const bool hasResultOrError = message.contains("result") || message.contains("error");

    if (hasMethod && hasId) {
        dispatchRequest(message);
    } else if (hasMethod) {
        dispatchNotification(message);
    } else if (hasResultOrError) {
        dispatchResponse(message);
    } else {
        qCWarning(llmMcpLog).noquote() << "Dropping unclassifiable JSON-RPC message";
        emit protocolError(QStringLiteral("Unclassifiable JSON-RPC message"));
    }
}

void JsonRpcSession::dispatchRequest(const QJsonObject &message)
{
    const QJsonValue idValue = message.value("id");
    QString idStr;
    if (idValue.isString())
        idStr = idValue.toString();
    else if (idValue.isDouble())
        idStr = QString::number(static_cast<qint64>(idValue.toDouble()));

    const QString method = message.value("method").toString();
    const QJsonObject params = message.value("params").toObject();

    const QJsonObject meta = params.value("_meta").toObject();
    const QJsonValue tokVal = meta.value("progressToken");
    QString progressToken;
    if (tokVal.isString())
        progressToken = tokVal.toString();
    else if (tokVal.isDouble())
        progressToken = QString::number(static_cast<qint64>(tokVal.toDouble()));

    m_currentProgressToken = progressToken;
    if (!idStr.isEmpty())
        m_inFlightIncomingIds.insert(idStr);

    qCDebug(llmMcpLog).noquote() << QString("<-- request method=%1").arg(method);
    emit incomingRequest(method);

    auto it = m_requestHandlers.find(method);
    if (it == m_requestHandlers.end()) {
        m_currentProgressToken.clear();
        m_inFlightIncomingIds.remove(idStr);
        sendError(idValue, Rpc::ErrorCode::MethodNotFound, QString("Method not found: %1").arg(method));
        return;
    }

    QFuture<QJsonValue> future;
    try {
        future = (*it)(params);
    } catch (const RemoteError &e) {
        m_currentProgressToken.clear();
        m_inFlightIncomingIds.remove(idStr);
        sendError(idValue, e.code(), e.remoteMessage(), e.data());
        return;
    } catch (const JsonRpcException &e) {
        m_currentProgressToken.clear();
        m_inFlightIncomingIds.remove(idStr);
        sendError(idValue, Rpc::ErrorCode::InternalError, e.message());
        return;
    } catch (const std::exception &e) {
        m_currentProgressToken.clear();
        m_inFlightIncomingIds.remove(idStr);
        sendError(idValue, Rpc::ErrorCode::InternalError, QString::fromUtf8(e.what()));
        return;
    } catch (...) {
        m_currentProgressToken.clear();
        m_inFlightIncomingIds.remove(idStr);
        sendError(idValue, Rpc::ErrorCode::InternalError, QStringLiteral("Unknown exception"));
        return;
    }

    m_currentProgressToken.clear();

    auto *watcher = new QFutureWatcher<QJsonValue>(this);
    QPointer<JsonRpcSession> guard(this);
    connect(
        watcher, &QFutureWatcher<QJsonValue>::finished, this, [guard, watcher, idValue, idStr]() {
            watcher->deleteLater();
            if (!guard)
                return;

            if (!idStr.isEmpty() && guard->m_cancelledIncomingIds.contains(idStr)) {
                guard->m_cancelledIncomingIds.remove(idStr);
                guard->m_inFlightIncomingIds.remove(idStr);
                return;
            }
            guard->m_inFlightIncomingIds.remove(idStr);

            QJsonValue result;
            bool ok = true;
            int errCode = Rpc::ErrorCode::InternalError;
            QString errMsg;
            QJsonValue errData;
            try {
                result = watcher->result();
            } catch (const RemoteError &e) {
                ok = false;
                errCode = e.code();
                errMsg = e.remoteMessage();
                errData = e.data();
            } catch (const CancelledError &e) {
                ok = false;
                errCode = Rpc::ErrorCode::RequestCancelled;
                errMsg = e.message();
            } catch (const JsonRpcException &e) {
                ok = false;
                errMsg = e.message();
            } catch (const std::exception &e) {
                ok = false;
                errMsg = QString::fromUtf8(e.what());
            } catch (...) {
                ok = false;
                errMsg = QStringLiteral("Unknown exception");
            }
            if (ok)
                guard->sendResponse(idValue, result);
            else
                guard->sendError(idValue, errCode, errMsg, errData);
        });
    watcher->setFuture(future);
}

void JsonRpcSession::dispatchResponse(const QJsonObject &message)
{
    const QJsonValue idValue = message.value("id");
    QString id;
    if (idValue.isString()) {
        id = idValue.toString();
    } else if (idValue.isDouble()) {
        id = QString::number(static_cast<qint64>(idValue.toDouble()));
    } else {
        qCDebug(llmMcpLog).noquote()
            << "Dropping response with null/missing id (non-spec-compliant server)";
        return;
    }

    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        qCWarning(llmMcpLog).noquote()
            << QString("Received response for unknown id: %1").arg(id);
        return;
    }

    auto promise = it->promise;
    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    if (!it->progressToken.isEmpty())
        clearProgressHandler(it->progressToken);
    m_pending.erase(it);

    if (message.contains("error")) {
        const QJsonObject err = message.value("error").toObject();
        const int code = err.value("code").toInt();
        const QString msg = err.value("message").toString();
        const QJsonValue data = err.value("data");
        qCDebug(llmMcpLog).noquote()
            << QString("<-- response id=%1 error=%2").arg(id).arg(msg);
        promise->setException(std::make_exception_ptr(RemoteError(code, msg, data)));
    } else {
        qCDebug(llmMcpLog).noquote() << QString("<-- response id=%1 ok").arg(id);
        promise->addResult(message.value("result"));
    }
    promise->finish();
}

void JsonRpcSession::dispatchNotification(const QJsonObject &message)
{
    const QString method = message.value("method").toString();
    const QJsonObject params = message.value("params").toObject();

    qCDebug(llmMcpLog).noquote() << QString("<-- notify method=%1").arg(method);

    auto it = m_notifyHandlers.find(method);
    if (it != m_notifyHandlers.end()) {
        try {
            (*it)(params);
        } catch (const std::exception &e) {
            qCWarning(llmMcpLog).noquote()
                << QString("Notification handler for %1 threw: %2").arg(method, e.what());
        }
    }
    emit notificationReceived(method, params);
}

void JsonRpcSession::sendResponse(const QJsonValue &id, const QJsonValue &result)
{
    if (!m_transport || !m_transport->isOpen())
        return;
    QJsonObject msg{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result},
    };
    m_transport->send(msg);
}

void JsonRpcSession::sendError(
    const QJsonValue &id, int code, const QString &message, const QJsonValue &data)
{
    if (!m_transport || !m_transport->isOpen())
        return;
    QJsonObject err{
        {"code", code},
        {"message", message},
    };
    if (!data.isNull() && !data.isUndefined())
        err.insert("data", data);
    QJsonObject msg{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", err},
    };
    m_transport->send(msg);
}

void JsonRpcSession::onTransportClosed()
{
    abortPending(QStringLiteral("Transport closed"));
}

} // namespace LLMQore::Rpc
