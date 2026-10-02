// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpHttpTransport.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

#include "McpHttpCommon.hpp"

namespace LLMQore::Mcp {

namespace {

constexpr int kInitialBackoffMs = 1000;
constexpr int kMaxBackoffMs = 30000;
constexpr int kSessionDeleteTimeoutMs = 5000;

bool answers(const QJsonObject &reply, const QJsonObject &request)
{
    const QJsonValue id = reply.value("id");
    return !reply.contains("method") && !id.isNull() && !id.isUndefined()
           && id.toVariant().toString() == request.value("id").toVariant().toString();
}

QJsonObject jsonRpcErrorIn(const QByteArray &body)
{
    const QJsonObject reply = QJsonDocument::fromJson(body).object();
    return reply.value("error").isObject() ? reply : QJsonObject{};
}

bool isTransientRefusal(int statusCode)
{
    return statusCode == 408 || statusCode == 409 || statusCode == 425 || statusCode == 429
           || (statusCode >= 500 && statusCode != 501);
}

} // namespace

struct McpStreamableHttpTransport::Impl
{
    struct Exchange
    {
        QJsonObject message;
        QPointer<HttpStreamHandle> stream;
        HttpResponse response;
        SSEParser events;
        bool answered = false;
        bool done = false;
        bool resuming = false;
        QByteArray cursor;
        std::optional<int> retryMs;
        int backoffMs = kInitialBackoffMs;
        QTimer *resumeTimer = nullptr;
    };

    McpStreamableHttpTransport *q = nullptr;
    HttpTransportConfig config;
    QPointer<LLMQore::HttpTransport> http;

    bool open = false;
    QString sessionId;
    QString protocolVersion;
    QList<std::shared_ptr<Exchange>> exchanges;

    QPointer<HttpStreamHandle> listenStream;
    SSEParser listenEvents;
    QByteArray lastEventId;
    bool listenAccepted = false;
    bool listenRefused = false;
    bool listenResumed = false;
    bool listenHeard = false;
    bool listenOutageReported = false;
    QList<SSEEvent> listenBacklog;
    int listenBackoffMs = kInitialBackoffMs;
    std::optional<int> serverRetryMs;
    QElapsedTimer listenUptime;
    QTimer *listenRetryTimer = nullptr;

    static bool isEventStream(const HttpResponse &response)
    {
        return response.isSuccess()
               && response.contentType().contains(QLatin1String("text/event-stream"));
    }

    void post(const QJsonObject &message)
    {
        if (!http) {
            failUnsent(message, QStringLiteral("HTTP transport destroyed"));
            return;
        }
        if (message.value("method").toString() == QLatin1String("initialize"))
            startNewSession();

        QNetworkRequest req(config.endpoint);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Accept", "application/json, text/event-stream");
        req.setTransferTimeout(config.requestTimeoutMs);
        applySessionHeaders(req);
        applyCustomHeaders(req, config.headers);

        const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);

        HttpStreamHandle *stream = http->openStream(req, QByteArrayView("POST"), body);
        if (!stream) {
            failUnsent(message, QStringLiteral("HTTP transport opened no stream"));
            return;
        }

        auto exchange = std::make_shared<Exchange>();
        exchange->message = message;
        exchanges.append(exchange);
        attach(exchange, stream);
    }

    void attach(const std::shared_ptr<Exchange> &exchange, HttpStreamHandle *stream)
    {
        exchange->stream = stream;
        QObject::connect(
            stream, &HttpStreamHandle::headersReceived, q, [this, exchange]() {
                onHeaders(*exchange);
            });
        QObject::connect(
            stream, &HttpStreamHandle::chunkReceived, q, [this, exchange](const QByteArray &chunk) {
                onChunk(exchange, chunk);
            });
        QObject::connect(
            stream, &HttpStreamHandle::finished, q, [this, exchange]() { onFinished(exchange); });
        QObject::connect(
            stream,
            &HttpStreamHandle::errorOccurred,
            q,
            [this, exchange](const HttpTransportError &e) {
                if (canResume(*exchange)) {
                    qCDebug(llmMcpLog).noquote()
                        << QString("Response stream dropped, resuming: %1").arg(e.message());
                    scheduleResume(exchange);
                    return;
                }
                release(exchange);
                fail(*exchange, QString("HTTP error: %1").arg(e.message()));
            });
        QObject::connect(stream, &QObject::destroyed, q, [this, exchange]() {
            release(exchange);
            fail(*exchange, QStringLiteral("HTTP stream destroyed before the reply ended"));
        });
    }

    void applySessionHeaders(QNetworkRequest &request) const
    {
        if (!sessionId.isEmpty())
            request.setRawHeader("Mcp-Session-Id", sessionId.toUtf8());
        if (!protocolVersion.isEmpty())
            request.setRawHeader("MCP-Protocol-Version", protocolVersion.toUtf8());
    }

    void onHeaders(Exchange &exchange)
    {
        exchange.response = responseHead(*exchange.stream);
        const QByteArray session = exchange.response.rawHeader(QByteArrayView("Mcp-Session-Id"));
        if (!session.isEmpty())
            sessionId = QString::fromUtf8(session);
    }

    void onChunk(const std::shared_ptr<Exchange> &held, const QByteArray &chunk)
    {
        Exchange &exchange = *held;
        if (!isEventStream(exchange.response)) {
            exchange.response.body.append(chunk);
            return;
        }
        const QList<SSEEvent> events = exchange.events.append(chunk);
        if (const std::optional<int> retry = exchange.events.retryMs())
            exchange.retryMs = retry;
        if (!events.isEmpty() || exchange.events.lastEventId() != exchange.cursor)
            exchange.backoffMs = kInitialBackoffMs;
        exchange.cursor = exchange.events.lastEventId();
        for (const SSEEvent &event : events) {
            if (exchange.done)
                return;
            if (event.type != QLatin1String("message"))
                continue;
            const QJsonObject reply = jsonRpcMessageIn(event);
            if (!reply.isEmpty())
                receive(exchange, reply);
        }
        if (exchange.resuming && exchange.answered && !exchange.done) {
            exchanges.removeOne(held);
            drop(exchange);
        }
    }

    void onFinished(const std::shared_ptr<Exchange> &exchange)
    {
        const HttpResponse &response = exchange->response;
        const bool resumable = isEventStream(response)
                               || (exchange->resuming && isTransientRefusal(response.statusCode));
        if (resumable && canResume(*exchange)) {
            scheduleResume(exchange);
            return;
        }
        release(exchange);

        if (!response.isSuccess()) {
            const QString reason = QString("HTTP error %1").arg(response.statusCode);
            qCWarning(llmMcpLog).noquote() << reason;
            emit q->errorOccurred(reason);
            if (isJsonRpcRequest(exchange->message))
                failRequest(exchange->message, reason, response.body);
            if (response.statusCode == 404 && !sessionId.isEmpty()) {
                sessionId.clear();
                q->stop();
            }
            return;
        }

        if (!isEventStream(response))
            deliverBody(*exchange);
        if (isJsonRpcRequest(exchange->message) && !exchange->answered) {
            emit q->sendFailed(
                exchange->message,
                QString("HTTP %1 reply carried no response to the request")
                    .arg(response.statusCode));
        }
    }

    static bool canResume(const Exchange &exchange)
    {
        return !exchange.done && !exchange.answered && !exchange.cursor.isEmpty()
               && isJsonRpcRequest(exchange.message);
    }

    void scheduleResume(const std::shared_ptr<Exchange> &exchange)
    {
        if (exchange->stream) {
            exchange->stream->disconnect(q);
            exchange->stream->deleteLater();
            exchange->stream = nullptr;
        }
        if (!exchange->resumeTimer) {
            exchange->resumeTimer = new QTimer(q);
            exchange->resumeTimer->setObjectName(QStringLiteral("resumeTimer"));
            exchange->resumeTimer->setSingleShot(true);
            const std::weak_ptr<Exchange> weak = exchange;
            QObject::connect(exchange->resumeTimer, &QTimer::timeout, q, [this, weak]() {
                if (const std::shared_ptr<Exchange> pending = weak.lock())
                    resume(pending);
            });
        }
        const int delayMs = (std::max)(exchange->retryMs.value_or(0), exchange->backoffMs);
        exchange->backoffMs = (std::min)(exchange->backoffMs * 2, kMaxBackoffMs);
        exchange->resumeTimer->start(delayMs);
    }

    void resume(const std::shared_ptr<Exchange> &exchange)
    {
        if (exchange->done)
            return;
        if (!http) {
            release(exchange);
            fail(*exchange, QStringLiteral("HTTP transport destroyed"));
            return;
        }

        QNetworkRequest req = eventStreamRequest(config);
        req.setTransferTimeout(config.requestTimeoutMs);
        applySessionHeaders(req);
        req.setRawHeader("Last-Event-ID", exchange->cursor);
        applyCustomHeaders(req, config.headers);

        HttpStreamHandle *stream = http->openStream(req, QByteArrayView("GET"));
        if (!stream) {
            release(exchange);
            fail(*exchange, QStringLiteral("HTTP transport opened no stream to resume the reply"));
            return;
        }
        exchange->resuming = true;
        exchange->response = {};
        exchange->events.clear();
        exchange->events.setLastEventId(exchange->cursor);
        attach(exchange, stream);
    }

    static void discardResumeTimer(Exchange &exchange)
    {
        if (QTimer *timer = std::exchange(exchange.resumeTimer, nullptr)) {
            timer->stop();
            timer->deleteLater();
        }
    }

    void deliverBody(Exchange &exchange)
    {
        const HttpResponse &response = exchange.response;
        if (response.statusCode == 202 || response.body.isEmpty())
            return;

        const QString contentType = response.contentType();
        if (!contentType.contains(QLatin1String("application/json"))) {
            qCWarning(llmMcpLog).noquote()
                << QString("Unexpected Content-Type: %1").arg(contentType);
            return;
        }

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            receive(exchange, doc.object());
            return;
        }
        qCWarning(llmMcpLog).noquote()
            << QString("Non-object JSON response: %1").arg(QString::fromUtf8(response.body));
        emit q->errorOccurred(QStringLiteral("Invalid JSON response body"));
    }

    void receive(Exchange &exchange, const QJsonObject &reply)
    {
        if (answers(reply, exchange.message)) {
            exchange.answered = true;
            if (exchange.message.value("method").toString() == QLatin1String("initialize")) {
                const QString version
                    = reply.value("result").toObject().value("protocolVersion").toString();
                if (!version.isEmpty()) {
                    protocolVersion = version;
                    openListenStream();
                }
            }
        }
        emit q->messageReceived(reply);
    }

    void failRequest(const QJsonObject &message, const QString &reason, const QByteArray &body)
    {
        QJsonObject error = jsonRpcErrorIn(body);
        if (error.isEmpty()) {
            emit q->sendFailed(message, reason);
            return;
        }
        error.insert("jsonrpc", "2.0");
        error.insert("id", message.value("id"));
        emit q->messageReceived(error);
    }

    void failUnsent(const QJsonObject &message, const QString &reason)
    {
        qCWarning(llmMcpLog).noquote() << reason;
        emit q->errorOccurred(reason);
        if (isJsonRpcRequest(message))
            emit q->sendFailed(message, reason);
    }

    void fail(const Exchange &exchange, const QString &reason)
    {
        qCWarning(llmMcpLog).noquote() << reason;
        emit q->errorOccurred(reason);
        if (isJsonRpcRequest(exchange.message) && !exchange.answered)
            emit q->sendFailed(exchange.message, reason);
    }

    void release(const std::shared_ptr<Exchange> &exchange)
    {
        exchange->done = true;
        discardResumeTimer(*exchange);
        exchanges.removeOne(exchange);
        if (exchange->stream) {
            exchange->stream->disconnect(q);
            exchange->stream->deleteLater();
        }
    }

    void openListenStream()
    {
        if (!open || !http || listenStream || listenRefused)
            return;

        QNetworkRequest req = eventStreamRequest(config);
        applySessionHeaders(req);
        if (!lastEventId.isEmpty())
            req.setRawHeader("Last-Event-ID", lastEventId);
        applyCustomHeaders(req, config.headers);

        listenEvents.clear();
        listenEvents.setLastEventId(lastEventId);
        listenResumed = !lastEventId.isEmpty();
        listenHeard = false;
        listenStream = http->openStream(req, QByteArrayView("GET"));
        if (!listenStream) {
            qCDebug(llmMcpLog).noquote()
                << QStringLiteral("Server message stream: the HTTP transport opened no stream");
            scheduleListenRetry();
            return;
        }

        HttpStreamHandle *stream = listenStream;
        QObject::connect(
            stream, &HttpStreamHandle::headersReceived, q, [this]() { onListenHeaders(); });
        QObject::connect(
            stream, &HttpStreamHandle::chunkReceived, q, [this](const QByteArray &chunk) {
                onListenChunk(chunk);
            });
        QObject::connect(stream, &HttpStreamHandle::finished, q, [this]() { onListenEnded(); });
        QObject::connect(
            stream, &HttpStreamHandle::errorOccurred, q, [this](const HttpTransportError &e) {
                qCDebug(llmMcpLog).noquote()
                    << QString("Server message stream dropped: %1").arg(e.message());
                onListenEnded();
            });
        QObject::connect(stream, &QObject::destroyed, q, [this]() { onListenEnded(); });
    }

    void onListenHeaders()
    {
        const HttpResponse head = responseHead(*listenStream);
        if (isEventStream(head)) {
            listenAccepted = true;
            listenUptime.start();
            return;
        }

        abandonListenStream();
        const int status = head.statusCode;
        if (status == 404 && listenAccepted && !sessionId.isEmpty()) {
            const QString reason
                = QString("Server message stream: session not found (HTTP %1)").arg(status);
            qCWarning(llmMcpLog).noquote() << reason;
            emit q->errorOccurred(reason);
            sessionId.clear();
            q->stop();
            return;
        }
        if (status == 400 && !lastEventId.isEmpty()) {
            lastEventId.clear();
            scheduleListenRetry();
            return;
        }
        if (isTransientRefusal(status)) {
            qCDebug(llmMcpLog).noquote()
                << QString("Server message stream refused for now (HTTP %1)").arg(status);
            scheduleListenRetry();
            return;
        }
        if (status != 405) {
            qCWarning(llmMcpLog).noquote()
                << QString("Server message stream unavailable (HTTP %1, %2)")
                       .arg(status)
                       .arg(head.contentType());
        }
        listenRefused = true;
    }

    void onListenChunk(const QByteArray &chunk)
    {
        const QList<SSEEvent> events = listenEvents.append(chunk);
        if (const std::optional<int> retry = listenEvents.retryMs())
            serverRetryMs = retry;
        if (!events.isEmpty() || listenEvents.lastEventId() != lastEventId)
            listenHeard = true;
        lastEventId = listenEvents.lastEventId();
        listenBacklog += events;
        while (!listenBacklog.isEmpty()) {
            const SSEEvent event = listenBacklog.takeFirst();
            if (event.type != QLatin1String("message"))
                continue;
            const QJsonObject message = jsonRpcMessageIn(event);
            if (message.isEmpty())
                continue;
            restartListenBackoff();
            emit q->messageReceived(message);
        }
    }

    void onListenEnded()
    {
        if (listenUptime.isValid()) {
            if (listenUptime.elapsed() >= kInitialBackoffMs)
                restartListenBackoff();
            else if (listenResumed && !listenHeard)
                lastEventId.clear();
        }
        listenUptime.invalidate();
        releaseListenStream();
        scheduleListenRetry();
    }

    void scheduleListenRetry()
    {
        if (!open || !http || listenRefused)
            return;
        const int delayMs = (std::max)(serverRetryMs.value_or(0), listenBackoffMs);
        if (listenBackoffMs >= kMaxBackoffMs && !listenOutageReported) {
            listenOutageReported = true;
            const QString reason
                = QString("Server message stream keeps failing; retrying every %1 s")
                      .arg(delayMs / 1000);
            qCWarning(llmMcpLog).noquote() << reason;
            emit q->errorOccurred(reason);
        }
        listenRetryTimer->start(delayMs);
        listenBackoffMs = (std::min)(listenBackoffMs * 2, kMaxBackoffMs);
    }

    void restartListenBackoff()
    {
        listenBackoffMs = kInitialBackoffMs;
        if (std::exchange(listenOutageReported, false))
            qCInfo(llmMcpLog).noquote() << QStringLiteral("Server message stream is back");
    }

    void releaseListenStream()
    {
        HttpStreamHandle *stream = listenStream;
        listenStream = nullptr;
        if (!stream)
            return;
        stream->disconnect(q);
        stream->deleteLater();
    }

    void abandonListenStream()
    {
        HttpStreamHandle *stream = listenStream;
        listenStream = nullptr;
        if (!stream)
            return;
        stream->disconnect(q);
        stream->abort();
        stream->deleteLater();
    }

    void stopListening()
    {
        listenRetryTimer->stop();
        abandonListenStream();
        listenEvents.clear();
        lastEventId.clear();
        listenAccepted = false;
        listenRefused = false;
        listenResumed = false;
        listenHeard = false;
        listenBacklog.clear();
        listenOutageReported = false;
        listenBackoffMs = kInitialBackoffMs;
        serverRetryMs.reset();
        listenUptime.invalidate();
    }

    void drop(Exchange &exchange)
    {
        exchange.done = true;
        discardResumeTimer(exchange);
        if (exchange.stream) {
            exchange.stream->disconnect(q);
            exchange.stream->abort();
            exchange.stream->deleteLater();
        }
    }

    QList<std::shared_ptr<Exchange>> abandonExchanges()
    {
        const QList<std::shared_ptr<Exchange>> pending = std::exchange(exchanges, {});
        for (const std::shared_ptr<Exchange> &exchange : pending)
            drop(*exchange);
        return pending;
    }

    void abandonExchange(const QString &requestId)
    {
        for (const std::shared_ptr<Exchange> &exchange : std::as_const(exchanges)) {
            if (isJsonRpcRequest(exchange->message)
                && exchange->message.value("id").toVariant().toString() == requestId) {
                const std::shared_ptr<Exchange> abandoned = exchange;
                exchanges.removeOne(abandoned);
                drop(*abandoned);
                return;
            }
        }
    }

    void deleteSession()
    {
        if (!http || sessionId.isEmpty())
            return;
        QNetworkRequest req(config.endpoint);
        req.setTransferTimeout((std::min)(kSessionDeleteTimeoutMs, config.requestTimeoutMs));
        applySessionHeaders(req);
        applyCustomHeaders(req, config.headers);
        (void) http->send(req, QByteArrayView("DELETE"));
    }

    void startNewSession()
    {
        for (const std::shared_ptr<Exchange> &exchange : abandonExchanges()) {
            if (isJsonRpcRequest(exchange->message) && !exchange->answered) {
                emit q->sendFailed(
                    exchange->message, QStringLiteral("Superseded by a new session"));
            }
        }
        deleteSession();
        sessionId.clear();
        protocolVersion.clear();
        stopListening();
    }
};

McpStreamableHttpTransport::McpStreamableHttpTransport(
    HttpTransportConfig config, LLMQore::HttpTransport *transport, QObject *parent)
    : Rpc::Transport(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->q = this;
    m_impl->config = std::move(config);
    m_impl->config.spec = McpHttpSpec::V2025_03_26;
    warnAboutProtocolHeaders(m_impl->config.headers);
    m_impl->http = resolveHttpTransport(transport, this, m_impl->config.requestTimeoutMs);
    m_impl->listenRetryTimer = new QTimer(this);
    m_impl->listenRetryTimer->setObjectName(QStringLiteral("listenRetryTimer"));
    m_impl->listenRetryTimer->setSingleShot(true);
    connect(m_impl->listenRetryTimer, &QTimer::timeout, this, [this]() {
        m_impl->openListenStream();
    });
    connect(m_impl->http, &QObject::destroyed, this, [this]() {
        m_impl->listenRetryTimer->stop();
    });
}

McpStreamableHttpTransport::~McpStreamableHttpTransport()
{
    stop();
}

void McpStreamableHttpTransport::start()
{
    if (m_impl->open)
        return;
    if (!m_impl->config.endpoint.isValid()) {
        emit errorOccurred(QStringLiteral("Invalid endpoint"));
        return;
    }
    m_impl->open = true;
}

void McpStreamableHttpTransport::stop()
{
    if (!m_impl->open)
        return;

    m_impl->open = false;
    m_impl->deleteSession();
    m_impl->sessionId.clear();
    m_impl->protocolVersion.clear();
    m_impl->abandonExchanges();
    m_impl->stopListening();
    emit closed();
}

bool McpStreamableHttpTransport::isOpen() const
{
    return m_impl->open;
}

void McpStreamableHttpTransport::send(const QJsonObject &message)
{
    if (!m_impl->open) {
        emit errorOccurred(QStringLiteral("Transport not open"));
        return;
    }
    m_impl->post(message);
}

void McpStreamableHttpTransport::abandon(const QString &requestId)
{
    m_impl->abandonExchange(requestId);
}

const HttpTransportConfig &McpStreamableHttpTransport::config() const
{
    return m_impl->config;
}

QString McpStreamableHttpTransport::sessionId() const
{
    return m_impl->sessionId;
}

} // namespace LLMQore::Mcp
