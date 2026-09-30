// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpHttpTransport.hpp>

#include <memory>
#include <utility>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>
#include <QPointer>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

#include "McpHttpCommon.hpp"

namespace LLMQore::Mcp {

namespace {

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
    LLMQore::HttpTransport *http = nullptr;

    bool open = false;
    QString sessionId;
    QString protocolVersion;
    QList<std::shared_ptr<Exchange>> exchanges;

    static bool isEventStream(const HttpResponse &response)
    {
        return response.isSuccess()
               && response.contentType().contains(QLatin1String("text/event-stream"));
    }

    void post(const QJsonObject &message)
    {
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
                if (!version.isEmpty())
                    protocolVersion = version;
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
