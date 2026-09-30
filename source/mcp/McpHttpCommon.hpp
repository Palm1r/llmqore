// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QObject>
#include <QString>

#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>
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

inline void applyCustomHeaders(QNetworkRequest &request, const QHash<QString, QString> &headers)
{
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
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
