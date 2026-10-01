// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpToolBinder.hpp>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpRemoteTool.hpp>
#include <LLMQore/RpcTransport.hpp>
#include <LLMQore/ToolRegistry.hpp>

#include <QSet>
#include <QTimer>

#include <chrono>

namespace LLMQore::Mcp {

namespace {

constexpr int kInitialBackoffMs = 1000;
constexpr int kMaxBackoffMs = 30000;
constexpr auto kInitializeTimeout = std::chrono::seconds(30);

QFuture<QList<ToolInfo>> listToolsOf(McpClient *client)
{
    if (client->isInitialized() && !client->serverInfo().capabilities.tools)
        return readyFuture(QList<ToolInfo>{});
    return client->listTools();
}

} // namespace

McpToolBinder::McpToolBinder(LLMQore::ToolRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
    Q_ASSERT(registry);
}

McpToolBinder::~McpToolBinder()
{
    m_stopping = true;
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
        if (it.key())
            disconnect(it.key(), nullptr, this, nullptr);
        clearTools(it.value());
    }
}

void McpToolBinder::setClientInfo(Implementation info)
{
    m_clientInfo = std::move(info);
}

bool McpToolBinder::addServer(const ServerEndpoint &endpoint)
{
    Rpc::Transport *transport = makeTransport(endpoint, this);
    if (!transport)
        return false;

    auto *client = new McpClient(transport, m_clientInfo, this);
    attach(client, endpoint.name, /*owned*/ true, /*autoReconnect*/ true);
    initializeClient(client);
    return true;
}

int McpToolBinder::loadServers(const QJsonObject &config)
{
    int added = 0;
    const QList<ServerEndpoint> endpoints = parseServerMap(config);
    for (const ServerEndpoint &endpoint : endpoints) {
        if (addServer(endpoint))
            ++added;
    }
    return added;
}

void McpToolBinder::addClient(McpClient *client, const QString &serverName, bool autoReconnect)
{
    if (!client) {
        qCWarning(llmMcpLog).noquote() << "Attempted to bind null McpClient";
        return;
    }
    if (m_bindings.contains(client))
        return;

    attach(client, serverName, /*owned*/ false, autoReconnect);
    resyncClient(client);
}

void McpToolBinder::removeClient(McpClient *client)
{
    auto it = m_bindings.find(client);
    if (it == m_bindings.end())
        return;
    clearTools(it.value());
    if (client)
        disconnect(client, nullptr, this, nullptr);
    m_bindings.erase(it);
}

void McpToolBinder::shutdown()
{
    m_stopping = true;
    const QList<McpClient *> clients = m_bindings.keys();
    for (McpClient *client : clients) {
        const auto it = m_bindings.constFind(client);
        if (it != m_bindings.cend() && it->owned && client)
            client->shutdown();
    }
}

void McpToolBinder::attach(McpClient *client, const QString &name, bool owned, bool autoReconnect)
{
    Binding binding;
    binding.name = name;
    binding.owned = owned;
    binding.autoReconnect = autoReconnect;
    binding.backoffMs = kInitialBackoffMs;
    m_bindings.insert(client, binding);

    connect(client, &McpClient::toolsChanged, this, [this, client]() {
        resyncClient(client);
    });

    connect(client, &QObject::destroyed, this, [this, client]() {
        auto it = m_bindings.find(client);
        if (it == m_bindings.end())
            return;
        clearTools(it.value());
        m_bindings.erase(it);
    });

    connect(client, &McpClient::disconnected, this, [this, client]() {
        auto it = m_bindings.find(client);
        if (it == m_bindings.end())
            return;
        emit serverDisconnected(it->name);
        if (it->autoReconnect && !m_stopping) {
            clearTools(it.value());
            scheduleReconnect(client);
        }
    });
}

void McpToolBinder::initializeClient(McpClient *client)
{
    QPointer<McpClient> guard(client);
    const auto bindingIt = m_bindings.constFind(client);
    const QString name = bindingIt != m_bindings.cend() ? bindingIt->name : QString();

    (void)LLMQore::compat(client->connectAndInitialize(kInitializeTimeout))
        .then(this, [guard](const InitializeResult &) {
            if (!guard)
                throw Rpc::TransportError(QStringLiteral("Client destroyed during initialize"));
            return listToolsOf(guard);
        })
        .unwrap()
        .then(this, [this, guard, name](const QList<ToolInfo> &tools) {
            auto it = guard ? m_bindings.find(guard) : m_bindings.end();
            if (it == m_bindings.end())
                return;
            it->reconnectPending = false;
            it->backoffMs = kInitialBackoffMs;
            applyTools(guard, tools);
            emit toolsSynced(name, tools.size());
            emit serverInitialized(name, guard->serverInfo());
        })
        .onFailed(this, [this, guard, name](const std::exception &e) {
            const QString error = QString::fromUtf8(e.what());
            qCWarning(llmMcpLog).noquote()
                << QString("MCP server '%1': initialize failed: %2").arg(name, error);
            emit serverInitFailed(name, error);
            auto it = guard ? m_bindings.find(guard) : m_bindings.end();
            if (it == m_bindings.end())
                return;
            it->reconnectPending = false;
            if (it->autoReconnect && !m_stopping) {
                it->backoffMs = (std::min)(it->backoffMs * 2, kMaxBackoffMs);
                scheduleReconnect(guard);
            }
        });
}

void McpToolBinder::resyncClient(McpClient *client)
{
    const auto it = m_bindings.constFind(client);
    if (it == m_bindings.cend())
        return;
    QPointer<McpClient> guard(client);
    const QString name = it->name;

    (void)LLMQore::compat(listToolsOf(client))
        .then(this, [this, guard, name](const QList<ToolInfo> &tools) {
            if (!guard || !m_bindings.contains(guard))
                return;
            applyTools(guard, tools);
            emit toolsSynced(name, tools.size());
        })
        .onFailed(this, [name](const std::exception &e) {
            qCWarning(llmMcpLog).noquote()
                << QString("MCP server '%1': tool listing failed: %2")
                       .arg(name, QString::fromUtf8(e.what()));
        });
}

void McpToolBinder::applyTools(McpClient *client, const QList<ToolInfo> &tools)
{
    auto bindingIt = m_bindings.find(client);
    if (bindingIt == m_bindings.end() || !m_registry)
        return;
    Binding &binding = bindingIt.value();

    QHash<QString, McpRemoteTool *> current;
    current.reserve(binding.tools.size());
    for (const QPointer<McpRemoteTool> &tool : std::as_const(binding.tools)) {
        if (tool)
            current.insert(tool->id(), tool.data());
    }

    QSet<QString> incoming;
    incoming.reserve(tools.size());
    QList<QPointer<McpRemoteTool>> next;
    next.reserve(tools.size());
    for (const ToolInfo &info : tools) {
        const QString id = McpRemoteTool::composeId(binding.name, info.name);
        incoming.insert(id);
        McpRemoteTool *mine = current.value(id, nullptr);
        if (mine && mine->info().toJson() == info.toJson()) {
            next.append(mine);
            continue;
        }
        LLMQore::BaseTool *registered = m_registry->tool(id);
        if (registered && registered != mine) {
            qCWarning(llmMcpLog).noquote()
                << QString("MCP server '%1': tool id '%2' is already registered by another "
                           "provider, skipping")
                       .arg(binding.name, id);
            continue;
        }
        if (mine)
            m_registry->removeTool(id);
        auto *tool = new McpRemoteTool(client, binding.name, info);
        m_registry->addTool(tool);
        next.append(tool);
    }

    for (auto it = current.cbegin(); it != current.cend(); ++it) {
        if (!incoming.contains(it.key()) && m_registry->tool(it.key()) == it.value())
            m_registry->removeTool(it.key());
    }
    binding.tools = std::move(next);
}

void McpToolBinder::clearTools(Binding &binding)
{
    if (m_registry) {
        for (const QPointer<McpRemoteTool> &tool : std::as_const(binding.tools)) {
            if (!tool)
                continue;
            const QString id = tool->id();
            if (m_registry->tool(id) == tool.data())
                m_registry->removeTool(id);
        }
    }
    binding.tools.clear();
}

void McpToolBinder::scheduleReconnect(McpClient *client)
{
    auto it = m_bindings.find(client);
    if (it == m_bindings.end() || it->reconnectPending || m_stopping)
        return;
    it->reconnectPending = true;

    qCInfo(llmMcpLog).noquote()
        << QString("MCP server '%1': disconnected, reconnect in %2 ms")
               .arg(it->name)
               .arg(it->backoffMs);

    QPointer<McpClient> guard(client);
    QTimer::singleShot(it->backoffMs, this, [this, guard]() {
        if (m_stopping || !guard || !m_bindings.contains(guard))
            return;
        initializeClient(guard);
    });
}

} // namespace LLMQore::Mcp
