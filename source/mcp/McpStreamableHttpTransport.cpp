// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpHttpTransport.hpp>

#include <algorithm>
#include <memory>
#include <utility>

#include <QByteArray>
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

constexpr int kInitialListenBackoffMs = 1000;
constexpr int kMaxListenBackoffMs = 30000;

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
    bool listenRefused = false;
    int listenBackoffMs = kInitialListenBackoffMs;
    QTimer *listenRetryTimer = nullptr;

    static bool isEventStream(const HttpResponse &response)
    {
        return response.isSuccess()
               && response.contentType().contains(QLatin1String("text/event-stream"));
    }

    void post(const QJsonObject &message)
    {
        if (!http) {
            const QString reason = QStringLiteral("HTTP transport destroyed");
            qCWarning(llmMcpLog).noquote() << reason;
            emit q->errorOccurred(reason);
            if (isJsonRpcRequest(message))
                emit q->sendFailed(message, reason);
            return;
        }

        QNetworkRequest req(config.endpoint);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Accept", "application/json, text/event-stream");
        req.setTransferTimeout(config.requestTimeoutMs);
        if (!sessionId.isEmpty())
            req.setRawHeader("Mcp-Session-Id", sessionId.toUtf8());
        if (!protocolVersion.isEmpty())
            req.setRawHeader("MCP-Protocol-Version", protocolVersion.toUtf8());
        applyCustomHeaders(req, config.headers);

        const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);

        auto exchange = std::make_shared<Exchange>();
        exchange->message = message;
        exchange->stream = http->openStream(req, QByteArrayView("POST"), body);
        exchanges.append(exchange);

        HttpStreamHandle *stream = exchange->stream;
        QObject::connect(
            stream, &HttpStreamHandle::headersReceived, q, [this, exchange]() {
                onHeaders(*exchange);
            });
        QObject::connect(
            stream, &HttpStreamHandle::chunkReceived, q, [this, exchange](const QByteArray &chunk) {
                onChunk(*exchange, chunk);
            });
        QObject::connect(
            stream, &HttpStreamHandle::finished, q, [this, exchange]() { onFinished(exchange); });
        QObject::connect(
            stream,
            &HttpStreamHandle::errorOccurred,
            q,
            [this, exchange](const HttpTransportError &e) {
                release(exchange);
                fail(*exchange, QString("HTTP error: %1").arg(e.message()));
            });
        QObject::connect(stream, &QObject::destroyed, q, [this, exchange]() {
            release(exchange);
            fail(*exchange, QStringLiteral("HTTP stream destroyed before the reply ended"));
        });
    }

    void onHeaders(Exchange &exchange)
    {
        exchange.response.statusCode = exchange.stream->statusCode();
        exchange.response.rawHeaders = exchange.stream->rawHeaders();
        const QByteArray session = exchange.response.rawHeader(QByteArrayView("Mcp-Session-Id"));
        if (!session.isEmpty())
            sessionId = QString::fromUtf8(session);
    }

    void onChunk(Exchange &exchange, const QByteArray &chunk)
    {
        if (!isEventStream(exchange.response)) {
            exchange.response.body.append(chunk);
            return;
        }
        for (const SSEEvent &event : exchange.events.append(chunk)) {
            if (exchange.done)
                return;
            if (event.type != QLatin1String("message"))
                continue;
            const QJsonObject reply = jsonRpcMessageIn(event);
            if (!reply.isEmpty())
                receive(exchange, reply);
        }
    }

    void onFinished(const std::shared_ptr<Exchange> &exchange)
    {
        release(exchange);
        const HttpResponse &response = exchange->response;

        if (!response.isSuccess()) {
            const QString reason = QString("HTTP error %1").arg(response.statusCode);
            qCWarning(llmMcpLog).noquote() << reason;
            emit q->errorOccurred(reason);
            if (isJsonRpcRequest(exchange->message))
                failRequest(exchange->message, reason, response.body);
            if (response.statusCode == 404 && !sessionId.isEmpty())
                q->stop();
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

        QNetworkRequest req(config.endpoint);
        req.setRawHeader("Accept", "text/event-stream");
        req.setRawHeader("Cache-Control", "no-cache");
        req.setTransferTimeout(config.sseIdleTimeoutMs);
        if (!sessionId.isEmpty())
            req.setRawHeader("Mcp-Session-Id", sessionId.toUtf8());
        if (!protocolVersion.isEmpty())
            req.setRawHeader("MCP-Protocol-Version", protocolVersion.toUtf8());
        if (!lastEventId.isEmpty())
            req.setRawHeader("Last-Event-ID", lastEventId);
        applyCustomHeaders(req, config.headers);

        listenEvents.clear();
        listenStream = http->openStream(req, QByteArrayView("GET"));

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
        HttpResponse head;
        head.statusCode = listenStream->statusCode();
        head.rawHeaders = listenStream->rawHeaders();
        if (isEventStream(head))
            return;

        if (head.statusCode != 405) {
            qCWarning(llmMcpLog).noquote()
                << QString("Server message stream unavailable (HTTP %1, %2)")
                       .arg(head.statusCode)
                       .arg(head.contentType());
        }
        listenRefused = true;
        abandonListenStream();
    }

    void onListenChunk(const QByteArray &chunk)
    {
        for (const SSEEvent &event : listenEvents.append(chunk)) {
            if (!listenStream)
                return;
            if (!event.id.isEmpty())
                lastEventId = event.id;
            if (event.type != QLatin1String("message"))
                continue;
            const QJsonObject message = jsonRpcMessageIn(event);
            if (message.isEmpty())
                continue;
            listenBackoffMs = kInitialListenBackoffMs;
            emit q->messageReceived(message);
        }
    }

    void onListenEnded()
    {
        releaseListenStream();
        if (!open || !http || listenRefused)
            return;
        listenRetryTimer->start(listenBackoffMs);
        listenBackoffMs = (std::min)(listenBackoffMs * 2, kMaxListenBackoffMs);
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
        listenRefused = false;
        listenBackoffMs = kInitialListenBackoffMs;
    }

    void abandonExchanges()
    {
        const QList<std::shared_ptr<Exchange>> pending = std::exchange(exchanges, {});
        for (const std::shared_ptr<Exchange> &exchange : pending) {
            exchange->done = true;
            if (exchange->stream) {
                exchange->stream->disconnect(q);
                exchange->stream->abort();
                exchange->stream->deleteLater();
            }
        }
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

const HttpTransportConfig &McpStreamableHttpTransport::config() const
{
    return m_impl->config;
}

QString McpStreamableHttpTransport::sessionId() const
{
    return m_impl->sessionId;
}

} // namespace LLMQore::Mcp
