// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpHttpTransport.hpp>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>
#include <QPointer>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

#include "McpHttpCommon.hpp"

namespace LLMQore::Mcp {

namespace {

int effectivePort(const QUrl &url)
{
    return url.port(url.scheme() == QLatin1String("https") ? 443 : 80);
}

bool isSameOrigin(const QUrl &url, const QUrl &origin)
{
    return url.scheme() == origin.scheme() && url.host() == origin.host()
           && effectivePort(url) == effectivePort(origin);
}

} // namespace

struct McpSseHttpTransport::Impl
{
    McpSseHttpTransport *q = nullptr;
    HttpTransportConfig config;
    LLMQore::HttpTransport *http = nullptr;

    bool open = false;

    QPointer<LLMQore::HttpStreamHandle> sseStream;
    SSEParser sseParser;
    QUrl postEndpoint;
    QList<QJsonObject> pendingSends;

    void openStream()
    {
        QNetworkRequest req = eventStreamRequest(config);
        applyCustomHeaders(req, config.headers);

        sseStream = http->openStream(req, QByteArrayView("GET"));

        QObject::connect(
            sseStream, &HttpStreamHandle::headersReceived, q, [this]() { onHeaders(); });
        QObject::connect(
            sseStream, &HttpStreamHandle::chunkReceived, q, [this](const QByteArray &chunk) {
                onChunk(chunk);
            });
        QObject::connect(sseStream, &HttpStreamHandle::finished, q, [this]() { onFinished(); });
        QObject::connect(sseStream, &QObject::destroyed, q, [this]() { onFinished(); });
        QObject::connect(
            sseStream, &HttpStreamHandle::errorOccurred, q, [this](const HttpTransportError &e) {
                const QString reason = QString("SSE stream error: %1").arg(e.message());
                qCWarning(llmMcpLog).noquote() << reason;
                emit q->errorOccurred(reason);
                onFinished();
            });

        open = true;
    }

    void onHeaders()
    {
        const HttpResponse head = responseHead(*sseStream);

        QString reason;
        if (!head.isSuccess())
            reason = QString("SSE stream rejected (HTTP %1)").arg(head.statusCode);
        else if (!head.contentType().contains(QLatin1String("text/event-stream")))
            reason = QString("SSE stream is not text/event-stream: %1").arg(head.contentType());
        if (reason.isEmpty())
            return;

        qCWarning(llmMcpLog).noquote() << reason;
        emit q->errorOccurred(reason);
        q->stop();
    }

    void onChunk(const QByteArray &chunk)
    {
        const QList<SSEEvent> events = sseParser.append(chunk);
        for (const SSEEvent &ev : events) {
            if (ev.type == QLatin1String("endpoint")) {
                QUrl ep(QString::fromUtf8(ev.data).trimmed());
                if (ep.isRelative())
                    ep = config.endpoint.resolved(ep);
                if (!isSameOrigin(ep, config.endpoint)) {
                    const QString reason = QString("SSE endpoint outside the connection origin: %1")
                                               .arg(ep.toString());
                    qCWarning(llmMcpLog).noquote() << reason;
                    emit q->errorOccurred(reason);
                    q->stop();
                    return;
                }
                postEndpoint = ep;
                qCDebug(llmMcpLog).noquote()
                    << QString("MCP POST endpoint resolved: %1").arg(ep.toString());

                const QList<QJsonObject> queued = std::move(pendingSends);
                pendingSends.clear();
                for (const QJsonObject &msg : queued)
                    post(msg);
            } else if (ev.type == QLatin1String("message")) {
                const QJsonObject message = jsonRpcMessageIn(ev);
                if (!message.isEmpty())
                    emit q->messageReceived(message);
            }
        }
    }

    void resetSession()
    {
        sseParser.clear();
        postEndpoint.clear();
        pendingSends.clear();
    }

    void onFinished()
    {
        if (sseStream) {
            sseStream->disconnect(q);
            sseStream->deleteLater();
            sseStream = nullptr;
        }
        resetSession();
        if (open) {
            open = false;
            emit q->closed();
        }
    }

    void post(const QJsonObject &message)
    {
        if (postEndpoint.isEmpty()) {
            pendingSends.append(message);
            return;
        }

        QNetworkRequest req(postEndpoint);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setTransferTimeout(config.requestTimeoutMs);
        applyCustomHeaders(req, config.headers);

        const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);

        (void) LLMQore::compat(http->send(req, QByteArrayView("POST"), body))
            .then(
                q,
                [this, message](const HttpResponse &response) {
                    if (!response.isSuccess()) {
                        const QString reason
                            = QString("POST failed (HTTP %1)").arg(response.statusCode);
                        qCWarning(llmMcpLog).noquote() << reason;
                        emit q->errorOccurred(reason);
                        if (isJsonRpcRequest(message))
                            emit q->sendFailed(message, reason);
                    }
                })
            .onFailed(q, [this, message](const HttpTransportError &e) {
                const QString reason = QString("POST failed: %1").arg(e.message());
                qCWarning(llmMcpLog).noquote() << reason;
                emit q->errorOccurred(reason);
                if (isJsonRpcRequest(message))
                    emit q->sendFailed(message, reason);
            });
    }
};

McpSseHttpTransport::McpSseHttpTransport(
    HttpTransportConfig config, LLMQore::HttpTransport *transport, QObject *parent)
    : Rpc::Transport(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->q = this;
    m_impl->config = std::move(config);
    m_impl->config.spec = McpHttpSpec::V2024_11_05;
    m_impl->http = resolveHttpTransport(transport, this, m_impl->config.requestTimeoutMs);
}

McpSseHttpTransport::~McpSseHttpTransport()
{
    stop();
}

void McpSseHttpTransport::start()
{
    if (m_impl->open)
        return;
    if (!m_impl->config.endpoint.isValid()) {
        emit errorOccurred(QStringLiteral("Invalid endpoint"));
        return;
    }
    m_impl->openStream();
}

void McpSseHttpTransport::stop()
{
    if (!m_impl->open)
        return;
    m_impl->open = false;

    if (m_impl->sseStream) {
        m_impl->sseStream->disconnect(this);
        m_impl->sseStream->abort();
        m_impl->sseStream->deleteLater();
        m_impl->sseStream = nullptr;
    }
    m_impl->resetSession();

    emit closed();
}

bool McpSseHttpTransport::isOpen() const
{
    return m_impl->open;
}

void McpSseHttpTransport::send(const QJsonObject &message)
{
    if (!m_impl->open) {
        emit errorOccurred(QStringLiteral("Transport not open"));
        return;
    }
    m_impl->post(message);
}

const HttpTransportConfig &McpSseHttpTransport::config() const
{
    return m_impl->config;
}

} // namespace LLMQore::Mcp
