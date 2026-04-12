// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpHttpTransport.hpp>

#include <LLMCore/HttpClient.hpp>
#include <LLMCore/HttpResponse.hpp>
#include <LLMCore/HttpStream.hpp>
#include <LLMCore/HttpTransportError.hpp>
#include <LLMCore/Log.hpp>
#include <LLMCore/SSEParser.hpp>

#include <QByteArray>
#include <QByteArrayList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>

namespace LLMCore::Mcp {

struct McpHttpTransport::Impl
{
    McpHttpTransport *q = nullptr;
    HttpTransportConfig config;

    LLMCore::HttpClient *http = nullptr;
    bool ownsHttp = false;

    bool open = false;

    QString sessionId;

    LLMCore::HttpStream *sseStream = nullptr;
    SSEParser sseParser;
    QUrl v2024PostEndpoint;
    QList<QJsonObject> pendingSends;

    void applyCustomHeaders(QNetworkRequest &req) const
    {
        for (auto it = config.headers.constBegin(); it != config.headers.constEnd(); ++it)
            req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    void startV2024()
    {
        QNetworkRequest req(config.endpoint);
        req.setRawHeader("Accept", "text/event-stream");
        req.setRawHeader("Cache-Control", "no-cache");
        applyCustomHeaders(req);

        sseStream = http->openStream(req, QByteArrayView("GET"));

        QObject::connect(
            sseStream, &HttpStream::chunkReceived, q, [this](const QByteArray &chunk) {
                onSseChunk(chunk);
            });
        QObject::connect(sseStream, &HttpStream::finished, q, [this]() { onSseFinished(); });
        QObject::connect(
            sseStream, &HttpStream::errorOccurred, q, [this](const HttpTransportError &e) {
                const QString reason = QString("SSE stream error: %1").arg(e.message());
                qCWarning(llmMcpLog).noquote() << reason;
                emit q->errorOccurred(reason);
                onSseFinished();
            });

        open = true;
    }

    void onSseChunk(const QByteArray &chunk)
    {
        const QList<SSEEvent> events = sseParser.append(chunk);
        for (const SSEEvent &ev : events) {
            if (ev.type == QLatin1String("endpoint")) {
                QString urlStr = QString::fromUtf8(ev.data).trimmed();
                QUrl ep(urlStr);
                if (ep.isRelative())
                    ep = config.endpoint.resolved(ep);
                v2024PostEndpoint = ep;
                qCDebug(llmMcpLog).noquote()
                    << QString("MCP POST endpoint resolved: %1").arg(ep.toString());

                q->setState(State::Connected);

                auto queued = std::move(pendingSends);
                pendingSends.clear();
                for (const QJsonObject &msg : queued)
                    postV2024(msg);
            } else if (
                ev.type == QLatin1String("message")
                || ev.type.isEmpty()) {
                QJsonParseError perr{};
                const QJsonDocument doc = QJsonDocument::fromJson(ev.data, &perr);
                if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
                    qCWarning(llmMcpLog).noquote()
                        << QString("SSE: cannot parse data as JSON: %1")
                               .arg(QString::fromUtf8(ev.data));
                    continue;
                }
                emit q->messageReceived(doc.object());
            }
        }
    }

    void onSseFinished()
    {
        if (sseStream) {
            sseStream->disconnect();
            sseStream->deleteLater();
            sseStream = nullptr;
        }
        if (open) {
            open = false;
            q->setState(State::Disconnected);
            emit q->closed();
        }
    }

    void postV2024(const QJsonObject &message)
    {
        if (v2024PostEndpoint.isEmpty()) {
            pendingSends.append(message);
            return;
        }
        QNetworkRequest req(v2024PostEndpoint);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        applyCustomHeaders(req);

        const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);

        http->send(req, QByteArrayView("POST"), body)
            .then(q, [this](const HttpResponse &response) {
                if (!response.isSuccess()) {
                    const QString reason
                        = QString("POST failed (HTTP %1)").arg(response.statusCode);
                    qCWarning(llmMcpLog).noquote() << reason;
                    emit q->errorOccurred(reason);
                }
            })
            .onFailed(q, [this](const HttpTransportError &e) {
                const QString reason = QString("POST failed: %1").arg(e.message());
                qCWarning(llmMcpLog).noquote() << reason;
                emit q->errorOccurred(reason);
            });
    }

    void startV2025()
    {
        open = true;
        q->setState(State::Connected);
    }

    void postV2025(const QJsonObject &message)
    {
        QNetworkRequest req(config.endpoint);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Accept", "application/json, text/event-stream");
        if (!sessionId.isEmpty())
            req.setRawHeader("Mcp-Session-Id", sessionId.toUtf8());
        applyCustomHeaders(req);

        const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);

        http->send(req, QByteArrayView("POST"), body)
            .then(q, [this](const HttpResponse &response) { handleV2025Response(response); })
            .onFailed(q, [this](const HttpTransportError &e) {
                const QString reason = QString("HTTP error: %1").arg(e.message());
                qCWarning(llmMcpLog).noquote() << reason;
                emit q->errorOccurred(reason);
            });
    }

    void handleV2025Response(const HttpResponse &response)
    {
        const QByteArray sessionHeader = response.rawHeader(QByteArrayView("Mcp-Session-Id"));
        if (!sessionHeader.isEmpty())
            sessionId = QString::fromUtf8(sessionHeader);

        if (!response.isSuccess()) {
            const QString reason = QString("HTTP error %1").arg(response.statusCode);
            qCWarning(llmMcpLog).noquote() << reason;
            emit q->errorOccurred(reason);
            return;
        }

        if (response.statusCode == 202 || response.body.isEmpty())
            return;

        const QString contentType = response.contentType();

        if (contentType.contains(QLatin1String("application/json"))) {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                emit q->messageReceived(doc.object());
            } else {
                qCWarning(llmMcpLog).noquote()
                    << QString("Non-object JSON response: %1")
                           .arg(QString::fromUtf8(response.body));
                emit q->errorOccurred(QStringLiteral("Invalid JSON response body"));
            }
        } else if (contentType.contains(QLatin1String("text/event-stream"))) {
            SSEParser parser;
            const QList<SSEEvent> events = parser.append(response.body);
            for (const SSEEvent &ev : events) {
                if (ev.type != QLatin1String("message") && !ev.type.isEmpty())
                    continue;
                QJsonParseError err{};
                const QJsonDocument doc = QJsonDocument::fromJson(ev.data, &err);
                if (err.error == QJsonParseError::NoError && doc.isObject())
                    emit q->messageReceived(doc.object());
            }
        } else {
            qCWarning(llmMcpLog).noquote()
                << QString("Unexpected Content-Type: %1").arg(contentType);
        }
    }
};

McpHttpTransport::McpHttpTransport(
    HttpTransportConfig config, LLMCore::HttpClient *httpClient, QObject *parent)
    : McpTransport(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->q = this;
    m_impl->config = std::move(config);

    if (httpClient) {
        m_impl->http = httpClient;
        m_impl->ownsHttp = false;
    } else {
        m_impl->http = new LLMCore::HttpClient(this);
        m_impl->ownsHttp = true;
    }
    m_impl->http->setTransferTimeout(m_impl->config.requestTimeoutMs);
}

McpHttpTransport::~McpHttpTransport()
{
    stop();
}

void McpHttpTransport::start()
{
    if (m_impl->open)
        return;
    if (!m_impl->config.endpoint.isValid()) {
        emit errorOccurred(QStringLiteral("Invalid endpoint"));
        setState(State::Failed);
        return;
    }
    setState(State::Connecting);
    switch (m_impl->config.spec) {
    case McpHttpSpec::V2025_03_26:
        m_impl->startV2025();
        break;
    case McpHttpSpec::V2024_11_05:
        m_impl->startV2024();
        break;
    }
}

void McpHttpTransport::stop()
{
    if (!m_impl->open)
        return;
    m_impl->open = false;

    if (m_impl->sseStream) {
        m_impl->sseStream->disconnect();
        m_impl->sseStream->abort();
        m_impl->sseStream->deleteLater();
        m_impl->sseStream = nullptr;
    }
    m_impl->sseParser.clear();
    m_impl->v2024PostEndpoint.clear();
    m_impl->pendingSends.clear();

    setState(State::Disconnected);
    emit closed();
}

bool McpHttpTransport::isOpen() const
{
    return m_impl->open;
}

void McpHttpTransport::send(const QJsonObject &message)
{
    if (!m_impl->open) {
        emit errorOccurred(QStringLiteral("Transport not open"));
        return;
    }

    switch (m_impl->config.spec) {
    case McpHttpSpec::V2025_03_26:
        m_impl->postV2025(message);
        break;
    case McpHttpSpec::V2024_11_05:
        m_impl->postV2024(message);
        break;
    }
}

const HttpTransportConfig &McpHttpTransport::config() const
{
    return m_impl->config;
}

} // namespace LLMCore::Mcp
