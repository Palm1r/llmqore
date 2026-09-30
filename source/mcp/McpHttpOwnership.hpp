// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QObject>
#include <QString>

#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpTransport.hpp>

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

} // namespace LLMQore::Mcp
