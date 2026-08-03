// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QNetworkRequest>
#include <QObject>
#include <QString>

#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpTransport.hpp>

namespace LLMQore::Mcp {

inline LLMQore::HttpTransport *resolveHttpTransport(
    LLMQore::HttpTransport *injected, QObject *owner, int requestTimeoutMs)
{
    LLMQore::HttpTransport *http = injected ? injected : new LLMQore::HttpClient(owner);
    http->setTransferTimeout(requestTimeoutMs);
    return http;
}

inline void applyCustomHeaders(QNetworkRequest &request, const QHash<QString, QString> &headers)
{
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
}

} // namespace LLMQore::Mcp
