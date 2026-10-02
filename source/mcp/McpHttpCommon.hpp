// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>

#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/McpHttpTransport.hpp>
#include <LLMQore/SSEEvent.hpp>

namespace LLMQore::Mcp {

inline LLMQore::HttpTransport *resolveHttpTransport(
    LLMQore::HttpTransport *injected, QObject *owner, int requestTimeoutMs)
{
    if (injected)
        return injected;
    auto *client = new LLMQore::HttpClient(owner);
    client->setTransferTimeout(requestTimeoutMs);
    return client;
}

inline bool isProtocolHeader(const QString &name)
{
    static const QStringList protocolHeaders{
        QStringLiteral("Accept"),
        QStringLiteral("Content-Type"),
        QStringLiteral("Mcp-Session-Id"),
        QStringLiteral("MCP-Protocol-Version"),
        QStringLiteral("Last-Event-ID"),
        QStringLiteral("Cache-Control"),
    };
    return protocolHeaders.contains(name.trimmed(), Qt::CaseInsensitive);
}

inline void warnAboutProtocolHeaders(const QHash<QString, QString> &headers)
{
    QStringList ignored;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        if (isProtocolHeader(it.key()))
            ignored.append(it.key());
    }
    if (ignored.isEmpty())
        return;
    ignored.sort(Qt::CaseInsensitive);
    qCWarning(llmMcpLog).noquote()
        << QString("Ignoring configured headers the MCP transport owns: %1")
               .arg(ignored.join(QStringLiteral(", ")));
}

inline void applyCustomHeaders(QNetworkRequest &request, const QHash<QString, QString> &headers)
{
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        if (!isProtocolHeader(it.key()))
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
}

inline QNetworkRequest eventStreamRequest(const HttpTransportConfig &config)
{
    QNetworkRequest request(config.endpoint);
    request.setRawHeader("Accept", "text/event-stream");
    request.setRawHeader("Cache-Control", "no-cache");
    request.setTransferTimeout(config.sseIdleTimeoutMs);
    return request;
}

inline HttpResponse responseHead(const HttpStreamHandle &stream)
{
    HttpResponse head;
    head.statusCode = stream.statusCode();
    head.rawHeaders = stream.rawHeaders();
    return head;
}

inline bool isJsonRpcRequest(const QJsonObject &message)
{
    return message.contains("method") && message.contains("id");
}

inline QJsonObject jsonRpcMessageIn(const SSEEvent &event)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(event.data, &error);
    if (error.error == QJsonParseError::NoError && doc.isObject())
        return doc.object();
    qCWarning(llmMcpLog).noquote()
        << QString("SSE: cannot parse data as JSON: %1").arg(QString::fromUtf8(event.data));
    return {};
}

} // namespace LLMQore::Mcp
