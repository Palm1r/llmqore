// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "BridgeServer.hpp"

#include <QTimer>

using namespace LLMCore::Mcp;

namespace {
constexpr int kInitialBackoffMs = 1000;
constexpr int kMaxBackoffMs = 30000;
} // namespace

namespace McpBridge {

BridgeServer::BridgeServer(const BridgeConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
    if (config.stdioMode) {
        m_serverTransport = new McpStdioServerTransport(this);
    } else {
        HttpServerConfig httpCfg;
        httpCfg.port = config.port;
        httpCfg.address = config.address;
        m_httpTransport = new McpHttpServerTransport(httpCfg, this);
        m_serverTransport = m_httpTransport;
    }

    McpServerConfig serverCfg;
    serverCfg.serverInfo = {"mcp-bridge", "0.1.0"};
    serverCfg.instructions = "MCP Bridge aggregating multiple upstream MCP servers.";

    m_server = new McpServer(m_serverTransport, serverCfg, this);
}

void BridgeServer::start()
{
    for (const UpstreamEntry &entry : m_config.upstreams) {
        McpTransport *transport = nullptr;

        if (entry.type == UpstreamType::Sse) {
            HttpTransportConfig httpCfg;
            httpCfg.endpoint = entry.url;
            httpCfg.spec = McpHttpSpec::V2024_11_05;
            httpCfg.headers = entry.headers;
            transport = new McpHttpTransport(httpCfg, nullptr, this);
        } else {
            StdioLaunchConfig launchCfg;
            launchCfg.program = entry.command;
            launchCfg.arguments = entry.args;
            launchCfg.environment = entry.env;
            transport = new McpStdioClientTransport(launchCfg, this);
        }

        auto *client = new McpClient(transport, Implementation{"mcp-bridge", "0.1.0"}, this);

        Upstream upstream;
        upstream.name = entry.name;
        upstream.transport = transport;
        upstream.client = client;
        m_upstreams.append(upstream);

        const QString name = entry.name;
        connect(client, &McpClient::errorOccurred, this, [name](const QString &err) {
            qWarning().noquote() << QString("[%1] error: %2").arg(name, err);
        });
    }

    m_pendingInits = m_upstreams.size();
    if (m_pendingInits == 0) {
        emit startFailed("No valid upstream servers to connect.");
        return;
    }

    for (int i = 0; i < m_upstreams.size(); ++i)
        connectUpstream(i);
}

void BridgeServer::shutdown()
{
    qInfo() << "Shutting down...";
    m_stopping = true;
    m_server->stop();
    for (auto &u : m_upstreams) {
        if (u.client)
            u.client->shutdown();
    }
}

quint16 BridgeServer::serverPort() const
{
    return m_httpTransport ? m_httpTransport->serverPort() : 0;
}

void BridgeServer::connectUpstream(int index)
{
    auto &upstream = m_upstreams[index];
    const QString name = upstream.name;

    qInfo().noquote() << QString("Connecting to [%1]...").arg(name);

    upstream.client->connectAndInitialize(std::chrono::seconds(30))
        .then(this,
              [this, index, name](const InitializeResult &result) {
                  qInfo().noquote() << QString("[%1] connected — %2 %3")
                                           .arg(name,
                                                result.serverInfo.name,
                                                result.serverInfo.version);
                  return m_upstreams[index].client->listTools();
              })
        .unwrap()
        .then(this,
              [this, index, name](const QList<ToolInfo> &tools) {
                  qInfo().noquote()
                      << QString("[%1] %2 tools discovered.").arg(name).arg(tools.size());
                  registerTools(index, tools);
                  ++m_completedInits;
                  checkAllReady();
              })
        .onFailed(this, [this, name](const std::exception &e) {
            qWarning().noquote() << QString("[%1] init failed: %2").arg(name, e.what());
            ++m_completedInits;
            checkAllReady();
        });

    connect(upstream.client, &McpClient::toolsChanged, this, [this, index]() {
        resyncTools(index);
    });

    connect(upstream.client, &McpClient::disconnected, this, [this, index]() {
        if (m_stopping)
            return;
        scheduleReconnect(index);
    });
}

void BridgeServer::clearTools(int index)
{
    auto &upstream = m_upstreams[index];
    for (auto *tool : upstream.tools)
        m_server->removeTool(tool->id());
    qDeleteAll(upstream.tools);
    upstream.tools.clear();
}

void BridgeServer::scheduleReconnect(int index)
{
    auto &upstream = m_upstreams[index];
    if (upstream.reconnectPending)
        return;
    upstream.reconnectPending = true;

    qWarning().noquote() << QString("[%1] upstream disconnected, reconnect in %2 ms")
                                .arg(upstream.name)
                                .arg(upstream.backoffMs);

    clearTools(index);

    QTimer::singleShot(upstream.backoffMs, this, [this, index]() {
        reconnectUpstream(index);
    });
}

void BridgeServer::reconnectUpstream(int index)
{
    if (m_stopping)
        return;

    auto &upstream = m_upstreams[index];
    const QString name = upstream.name;
    qInfo().noquote() << QString("[%1] reconnecting...").arg(name);

    upstream.client->connectAndInitialize(std::chrono::seconds(30))
        .then(this,
              [this, index, name](const InitializeResult &result) {
                  qInfo().noquote() << QString("[%1] reconnected — %2 %3")
                                           .arg(name,
                                                result.serverInfo.name,
                                                result.serverInfo.version);
                  return m_upstreams[index].client->listTools();
              })
        .unwrap()
        .then(this,
              [this, index, name](const QList<ToolInfo> &tools) {
                  auto &u = m_upstreams[index];
                  registerTools(index, tools);
                  qInfo().noquote()
                      << QString("[%1] re-synced: %2 tools.").arg(name).arg(tools.size());
                  u.reconnectPending = false;
                  u.backoffMs = kInitialBackoffMs;
              })
        .onFailed(this, [this, index, name](const std::exception &e) {
            auto &u = m_upstreams[index];
            qWarning().noquote() << QString("[%1] reconnect failed: %2").arg(name, e.what());
            u.reconnectPending = false;
            u.backoffMs = std::min(u.backoffMs * 2, kMaxBackoffMs);
            if (!m_stopping)
                scheduleReconnect(index);
        });
}

void BridgeServer::registerTools(int index, const QList<ToolInfo> &tools)
{
    auto &upstream = m_upstreams[index];
    for (const ToolInfo &ti : tools) {
        auto *remoteTool = new McpRemoteTool(upstream.client, ti, this);
        upstream.tools.append(remoteTool);
        m_server->addTool(remoteTool);
        qInfo().noquote() << QString("  + %1: %2").arg(ti.name, ti.description.left(60));
    }
}

void BridgeServer::resyncTools(int index)
{
    auto &upstream = m_upstreams[index];
    const QString name = upstream.name;
    qInfo().noquote() << QString("[%1] tools changed, re-syncing...").arg(name);

    clearTools(index);

    upstream.client->listTools().then(this, [this, index, name](const QList<ToolInfo> &tools) {
        registerTools(index, tools);
        qInfo().noquote() << QString("[%1] re-synced: %2 tools.").arg(name).arg(tools.size());
    });
}

void BridgeServer::checkAllReady()
{
    if (m_completedInits < m_pendingInits)
        return;

    int totalTools = 0;
    for (const auto &u : m_upstreams)
        totalTools += u.tools.size();

    m_server->start();

    QString url;
    if (m_httpTransport) {
        url = QString("http://%1:%2%3")
                  .arg(m_config.address.toString())
                  .arg(m_httpTransport->serverPort())
                  .arg(m_httpTransport->config().path);
        qInfo().noquote() << QString("MCP Bridge listening on %1").arg(url);
    } else {
        url = QStringLiteral("stdio");
        qInfo().noquote() << "MCP Bridge serving over stdio.";
    }

    qInfo().noquote() << QString("Aggregating %1 tools from %2 upstream servers.")
                             .arg(totalTools)
                             .arg(m_upstreams.size());

    emit ready(url);
}

} // namespace McpBridge
