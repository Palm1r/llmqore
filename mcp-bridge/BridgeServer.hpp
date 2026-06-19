// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QObject>

#include <LLMQore/Mcp>

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
        LLMQore::Rpc::Transport *transport = nullptr;
        LLMQore::Mcp::McpClient *client = nullptr;
        QList<LLMQore::Mcp::McpRemoteTool *> tools;
        bool reconnectPending = false;
        int backoffMs = 1000;
    };

    void connectUpstream(int index);
    void registerTools(int index, const QList<LLMQore::Mcp::ToolInfo> &tools);
    void clearTools(int index);
    void resyncTools(int index);
    void scheduleReconnect(int index);
    void reconnectUpstream(int index);
    void checkAllReady();

    BridgeConfig m_config;
    LLMQore::Rpc::Transport *m_serverTransport = nullptr;
    LLMQore::Mcp::McpHttpServerTransport *m_httpTransport = nullptr;
    LLMQore::Mcp::McpServer *m_server = nullptr;
    QList<Upstream> m_upstreams;
    int m_pendingInits = 0;
    int m_completedInits = 0;
    bool m_stopping = false;
};

} // namespace McpBridge
