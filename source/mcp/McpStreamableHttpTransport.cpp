// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpHttpTransport.hpp>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkRequest>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

#include "McpHttpOwnership.hpp"

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
    McpStreamableHttpTransport *q = nullptr;
    HttpTransportConfig config;
    LLMQore::HttpTransport *http = nullptr;

    bool open = false;
    QString sessionId;
    quint64 generation = 0;

    void post(const QJsonObject &message)
    {
        QNetworkRequest req(config.endpoint);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Accept", "application/json, text/event-stream");
        req.setTransferTimeout(config.requestTimeoutMs);
        if (!sessionId.isEmpty())
            req.setRawHeader("Mcp-Session-Id", sessionId.toUtf8());
        applyCustomHeaders(req, config.headers);

        const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
        const quint64 postedIn = generation;

        (void) LLMQore::compat(http->send(req, QByteArrayView("POST"), body))
            .then(
                q,
                [this, postedIn, message](const HttpResponse &response) {
                    if (postedIn == generation)
                        handleResponse(message, response);
                })
            .onFailed(q, [this, postedIn, message](const HttpTransportError &e) {
                if (postedIn != generation)
                    return;
                const QString reason = QString("HTTP error: %1").arg(e.message());
                qCWarning(llmMcpLog).noquote() << reason;
                emit q->errorOccurred(reason);
                if (isJsonRpcRequest(message))
                    emit q->sendFailed(message, reason);
            });
    }

    void handleResponse(const QJsonObject &message, const HttpResponse &response)
    {
        const QByteArray sessionHeader = response.rawHeader(QByteArrayView("Mcp-Session-Id"));
        if (!sessionHeader.isEmpty())
            sessionId = QString::fromUtf8(sessionHeader);

        if (!response.isSuccess()) {
            const QString reason = QString("HTTP error %1").arg(response.statusCode);
            qCWarning(llmMcpLog).noquote() << reason;
            emit q->errorOccurred(reason);
            if (isJsonRpcRequest(message))
                failRequest(message, reason, response.body);
            if (response.statusCode == 404 && !sessionId.isEmpty())
                q->stop();
            return;
        }

        if (!deliver(message, response) && isJsonRpcRequest(message)) {
            emit q->sendFailed(
                message,
                QString("HTTP %1 reply carried no response to the request")
                    .arg(response.statusCode));
        }
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

    bool deliver(const QJsonObject &message, const HttpResponse &response)
    {
        if (response.statusCode == 202 || response.body.isEmpty())
            return false;

        bool answered = false;
        const auto receive = [&](const QJsonObject &reply) {
            answered = answered || answers(reply, message);
            emit q->messageReceived(reply);
        };

        const QString contentType = response.contentType();

        if (contentType.contains(QLatin1String("application/json"))) {
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(response.body, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                receive(doc.object());
            } else {
                qCWarning(llmMcpLog).noquote()
                    << QString("Non-object JSON response: %1").arg(QString::fromUtf8(response.body));
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
                    receive(doc.object());
            }
        } else {
            qCWarning(llmMcpLog).noquote()
                << QString("Unexpected Content-Type: %1").arg(contentType);
        }
        return answered;
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
    ++m_impl->generation;
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
