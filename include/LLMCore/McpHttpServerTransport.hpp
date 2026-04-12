// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <QHostAddress>
#include <QList>
#include <QString>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTransport.hpp>

namespace LLMCore::Mcp {

struct LLMCORE_EXPORT HttpServerConfig
{
    QHostAddress address = QHostAddress::LocalHost;
    quint16 port = 0;
    QString path = QStringLiteral("/mcp");
    QList<QString> allowedOrigins;

    qint64 maxBodyBytes = 8 * 1024 * 1024;
    qint64 maxHeaderBytes = 32 * 1024;
    int pendingResponseTimeoutMs = 120000;
    int maxQueuedServerMessages = 1024;
};

class LLMCORE_EXPORT McpHttpServerTransport : public McpTransport
{
    Q_OBJECT
public:
    explicit McpHttpServerTransport(HttpServerConfig config, QObject *parent = nullptr);
    ~McpHttpServerTransport() override;

    void start() override;
    void stop() override;
    bool isOpen() const override;
    void send(const QJsonObject &message) override;

    quint16 serverPort() const;

    const HttpServerConfig &config() const;
    QString sessionId() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMCore::Mcp
