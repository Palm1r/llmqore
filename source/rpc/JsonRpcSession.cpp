// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/JsonRpcSession.hpp>

#include <LLMQore/Log.hpp>
#include <LLMQore/RpcExceptions.hpp>

#include <QFutureWatcher>
#include <QJsonDocument>
#include <QTimer>

namespace LLMQore::Rpc {

MessageKind classify(const QJsonObject &message)
{
    const bool hasId = message.contains(QLatin1String("id"))
        && !message.value(QLatin1String("id")).isNull();
    const bool hasMethod = message.contains(QLatin1String("method"));
    const bool hasResultOrError = message.contains(QLatin1String("result"))
        || message.contains(QLatin1String("error"));

    if (hasMethod && hasId)
        return MessageKind::Request;
    if (hasMethod)
        return MessageKind::Notification;
    if (hasResultOrError)
        return MessageKind::Response;
    return MessageKind::Invalid;
}

QString idToString(const QJsonValue &id)
{
    if (id.isString())
        return id.toString();
    if (id.isDouble())
        return QString::number(static_cast<qint64>(id.toDouble()));
    return {};
}

namespace {

struct ErrorAnswer
{
    int code = Rpc::ErrorCode::InternalError;
    QString message;
    QJsonValue data;
};

ErrorAnswer errorAnswerFor(std::exception_ptr exception)
{
    try {
        std::rethrow_exception(exception);
    } catch (const RemoteError &e) {
        return {e.code(), e.remoteMessage(), e.data()};
    } catch (const CancelledError &e) {
        return {Rpc::ErrorCode::RequestCancelled, e.message(), {}};
    } catch (const JsonRpcException &e) {
        return {Rpc::ErrorCode::InternalError, e.message(), {}};
    } catch (const std::exception &e) {
        return {Rpc::ErrorCode::InternalError, QString::fromUtf8(e.what()), {}};
    } catch (...) {
        return {Rpc::ErrorCode::InternalError, QStringLiteral("Unknown exception"), {}};
    }
}

void logWire(const char *direction, const QJsonObject &message)
{
    if (!llmRpcWireLog().isDebugEnabled())
        return;
    QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    constexpr int kMaxWireLogBytes = 8192;
    if (json.size() > kMaxWireLogBytes)
        json = json.left(kMaxWireLogBytes) + "... (truncated)";
    qCDebug(llmRpcWireLog).noquote() << direction << QString::fromUtf8(json);
}

} // namespace

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
        connect(m_transport, &Transport::sendFailed, this, &JsonRpcSession::onSendFailed);
        connect(m_transport, &Transport::closed, this, &JsonRpcSession::onTransportClosed);
    }

    setNotificationHandler(
        QLatin1String(Method::Cancelled), [this](const QJsonObject &params) {
            const QString id = idToString(params.value("requestId"));
            if (id.isEmpty())
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
        QLatin1String(Method::Progress), [this](const QJsonObject &params) {
            const QString token = idToString(params.value("progressToken"));
            const double progress = params.value("progress").toDouble();
            const double total = params.value("total").toDouble();
            const QString message = params.value("message").toString();
            emit progressReceived(token, progress, total, message);

            auto it = m_progressHandlers.find(token);
            if (it != m_progressHandlers.end()) {
                try {
                    it.value()(progress, total, message);
                } catch (const std::exception &e) {
                    qCWarning(llmRpcLog).noquote()
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

    Pending pending{promise, timer, trackProgressToken ? id : QString(), method};
    m_pending.insert(id, pending);

    connect(timer, &QTimer::timeout, this, [this, id]() {
        auto it = m_pending.find(id);
        if (it == m_pending.end())
            return;
        auto p = it->promise;
        const QString method = it->method;
        if (it->timer)
            it->timer->deleteLater();
        if (!it->progressToken.isEmpty())
            clearProgressHandler(it->progressToken);
        m_pending.erase(it);
        qCWarning(llmRpcLog).noquote() << QString("Request %1 timed out").arg(id);
        abandonRequest(id, method, QStringLiteral("Request timed out"));
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

    qCDebug(llmRpcLog).noquote()
        << QString("--> request id=%1 method=%2%3")
               .arg(id,
                    method,
                    trackProgressToken ? QStringLiteral(" (cancellable)") : QString());
    logWire("-->", message);
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

    auto p = it->promise;
    const QString method = it->method;
    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    if (!it->progressToken.isEmpty())
        clearProgressHandler(it->progressToken);
    m_pending.erase(it);
    abandonRequest(id, method, reason);
    p->setException(std::make_exception_ptr(CancelledError(
        reason.isEmpty() ? QStringLiteral("Cancelled by caller") : reason)));
    p->finish();
}

void JsonRpcSession::abandonRequest(
    const QString &id, const QString &method, const QString &reason)
{
    if (!m_transport || !m_transport->isOpen())
        return;
    if (method != QLatin1String("initialize")) {
        QJsonObject params{{"requestId", id}};
        if (!reason.isEmpty())
            params.insert("reason", reason);
        sendNotification(QLatin1String(Method::Cancelled), params);
    }
    m_transport->abandon(id);
}

void JsonRpcSession::sendNotification(const QString &method, const QJsonObject &params)
{
    if (!m_transport || !m_transport->isOpen()) {
        qCWarning(llmRpcLog).noquote()
            << QString("Dropping notification %1: transport not open").arg(method);
        return;
    }

    QJsonObject message{
        {"jsonrpc", "2.0"},
        {"method", method},
    };
    if (!params.isEmpty())
        message.insert("params", params);

    qCDebug(llmRpcLog).noquote() << QString("--> notify method=%1").arg(method);
    logWire("-->", message);
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
    sendNotification(QLatin1String(Method::Progress), params);
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
    logWire("<--", message);
    const QString jsonrpc = message.value("jsonrpc").toString();
    if (jsonrpc != QLatin1String("2.0")) {
        qCWarning(llmRpcLog).noquote()
            << QString("Dropping message with unexpected jsonrpc field: %1").arg(jsonrpc);
        emit protocolError(QStringLiteral("Invalid jsonrpc field"));
        return;
    }

    switch (classify(message)) {
    case MessageKind::Request:
        dispatchRequest(message);
        break;
    case MessageKind::Notification:
        dispatchNotification(message);
        break;
    case MessageKind::Response:
        dispatchResponse(message);
        break;
    case MessageKind::Invalid:
        qCWarning(llmRpcLog).noquote() << "Dropping unclassifiable JSON-RPC message";
        sendError(
            message.value("id"),
            Rpc::ErrorCode::InvalidRequest,
            QStringLiteral("Invalid Request"));
        emit protocolError(QStringLiteral("Unclassifiable JSON-RPC message"));
        break;
    }
}

void JsonRpcSession::dispatchRequest(const QJsonObject &message)
{
    const QJsonValue idValue = message.value("id");
    const QString idStr = idToString(idValue);

    const QString method = message.value("method").toString();
    const QJsonObject params = message.value("params").toObject();

    const QJsonObject meta = params.value("_meta").toObject();
    m_currentProgressToken = idToString(meta.value("progressToken"));
    if (!idStr.isEmpty())
        m_inFlightIncomingIds.insert(idStr);

    qCDebug(llmRpcLog).noquote() << QString("<-- request method=%1").arg(method);
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
    } catch (...) {
        m_currentProgressToken.clear();
        m_inFlightIncomingIds.remove(idStr);
        const ErrorAnswer answer = errorAnswerFor(std::current_exception());
        sendError(idValue, answer.code, answer.message, answer.data);
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

            try {
                guard->sendResponse(idValue, watcher->result());
            } catch (...) {
                const ErrorAnswer answer = errorAnswerFor(std::current_exception());
                guard->sendError(idValue, answer.code, answer.message, answer.data);
            }
        });
    watcher->setFuture(future);
}

void JsonRpcSession::dispatchResponse(const QJsonObject &message)
{
    const QString id = idToString(message.value("id"));
    if (id.isEmpty()) {
        qCDebug(llmRpcLog).noquote()
            << "Dropping response with null/missing id (non-spec-compliant server)";
        return;
    }

    auto it = m_pending.find(id);
    if (it == m_pending.end()) {
        qCWarning(llmRpcLog).noquote()
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
        qCDebug(llmRpcLog).noquote()
            << QString("<-- response id=%1 error=%2").arg(id).arg(msg);
        promise->setException(std::make_exception_ptr(RemoteError(code, msg, data)));
    } else {
        qCDebug(llmRpcLog).noquote() << QString("<-- response id=%1 ok").arg(id);
        promise->addResult(message.value("result"));
    }
    promise->finish();
}

void JsonRpcSession::dispatchNotification(const QJsonObject &message)
{
    const QString method = message.value("method").toString();
    const QJsonObject params = message.value("params").toObject();

    qCDebug(llmRpcLog).noquote() << QString("<-- notify method=%1").arg(method);

    auto it = m_notifyHandlers.find(method);
    if (it != m_notifyHandlers.end()) {
        try {
            (*it)(params);
        } catch (const std::exception &e) {
            qCWarning(llmRpcLog).noquote()
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
    logWire("-->", msg);
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
    logWire("-->", msg);
    m_transport->send(msg);
}

void JsonRpcSession::onSendFailed(const QJsonObject &message, const QString &reason)
{
    if (!message.contains("method"))
        return;

    auto it = m_pending.find(idToString(message.value("id")));
    if (it == m_pending.end())
        return;

    auto promise = it->promise;
    if (it->timer) {
        it->timer->stop();
        it->timer->deleteLater();
    }
    if (!it->progressToken.isEmpty())
        clearProgressHandler(it->progressToken);
    m_pending.erase(it);

    promise->setException(std::make_exception_ptr(TransportError(reason)));
    promise->finish();
}

void JsonRpcSession::onTransportClosed()
{
    abortPending(QStringLiteral("Transport closed"));
}

} // namespace LLMQore::Rpc
