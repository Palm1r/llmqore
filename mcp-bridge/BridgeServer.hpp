// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QObject>

#include <LLMCore/Mcp>

#include "BridgeConfig.hpp"

namespace McpBridge {

class BridgeServer : public QObject
{
    Q_OBJECT
public:
    explicit BridgeServer(const BridgeConfig &config, QObject *parent = nullptr);

    void start();
    void shutdown();

    quint16 serverPort() const;

signals:
    void ready(const QString &url);
    void startFailed(const QString &reason);

private:
    struct Upstream
    {
        QString name;
        LLMCore::Mcp::McpTransport *transport = nullptr;
        LLMCore::Mcp::McpClient *client = nullptr;
        QList<LLMCore::Mcp::McpRemoteTool *> tools;
        bool reconnectPending = false;
        int backoffMs = 1000;
    };

    void connectUpstream(int index);
    void registerTools(int index, const QList<LLMCore::Mcp::ToolInfo> &tools);
    void clearTools(int index);
    void resyncTools(int index);
    void scheduleReconnect(int index);
    void reconnectUpstream(int index);
    void checkAllReady();

    BridgeConfig m_config;
    LLMCore::Mcp::McpTransport *m_serverTransport = nullptr;
    LLMCore::Mcp::McpHttpServerTransport *m_httpTransport = nullptr;
    LLMCore::Mcp::McpServer *m_server = nullptr;
    QList<Upstream> m_upstreams;
    int m_pendingInits = 0;
    int m_completedInits = 0;
    bool m_stopping = false;
};

} // namespace McpBridge
