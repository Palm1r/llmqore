// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpToolBinder.hpp>

#include <LLMCore/Log.hpp>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpExceptions.hpp>
#include <LLMCore/McpRemoteTool.hpp>
#include <LLMCore/ToolsManager.hpp>

#include <QPromise>
#include <QSet>

namespace LLMCore::Mcp {

McpToolBinder::McpToolBinder(McpClient *client, LLMCore::ToolsManager *target, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_target(target)
{
    if (m_client) {
        connect(m_client, &McpClient::toolsChanged, this, &McpToolBinder::refreshTools);
    }
}

QFuture<void> McpToolBinder::bind()
{
    auto promise = std::make_shared<QPromise<void>>();
    promise->start();

    if (!m_client || !m_target) {
        const QString msg = QStringLiteral("Client or ToolsManager is null");
        emit failed(msg);
        promise->setException(std::make_exception_ptr(McpException(msg)));
        promise->finish();
        return promise->future();
    }

    auto failPromise = [this, promise](const QString &msg) {
        emit failed(msg);
        promise->setException(std::make_exception_ptr(McpException(msg)));
        promise->finish();
    };

    auto listAndRegister = [this, promise, failPromise]() {
        m_client->listTools()
            .then(this,
                  [this, promise](const QList<ToolInfo> &tools) {
                      registerToolsFromList(tools);
                      emit bound(tools.size());
                      promise->finish();
                  })
            .onFailed(this, [failPromise](const std::exception &e) {
                failPromise(QString::fromUtf8(e.what()));
            });
    };

    if (m_client->isInitialized()) {
        listAndRegister();
    } else {
        m_client->connectAndInitialize()
            .then(this,
                  [listAndRegister](const InitializeResult &) { listAndRegister(); })
            .onFailed(this, [failPromise](const std::exception &e) {
                failPromise(QString::fromUtf8(e.what()));
            });
    }

    return promise->future();
}

void McpToolBinder::refreshTools()
{
    if (!m_client || !m_target)
        return;

    m_client->listTools()
        .then(this, [this](const QList<ToolInfo> &tools) { registerToolsFromList(tools); })
        .onFailed(this, [](const std::exception &e) {
            qCWarning(llmMcpLog).noquote()
                << QString("Failed to refresh MCP tools: %1").arg(e.what());
        });
}

void McpToolBinder::registerToolsFromList(const QList<ToolInfo> &tools)
{
    if (!m_target)
        return;

    QSet<QString> incomingNames;
    incomingNames.reserve(tools.size());
    for (const ToolInfo &info : tools)
        incomingNames.insert(info.name);

    for (const QString &oldName : std::as_const(m_currentlyRegistered)) {
        if (!incomingNames.contains(oldName))
            m_target->removeTool(oldName);
    }

    QStringList newlyRegistered;
    newlyRegistered.reserve(tools.size());
    for (const ToolInfo &info : tools) {
        if (m_target->tool(info.name)) {
            qCDebug(llmMcpLog).noquote()
                << QString("Tool '%1' already registered in ToolsManager, replacing with MCP tool")
                       .arg(info.name);
            m_target->removeTool(info.name);
        }
        m_target->addTool(new McpRemoteTool(m_client.data(), info));
        newlyRegistered.append(info.name);
    }

    m_currentlyRegistered = std::move(newlyRegistered);
    qCDebug(llmMcpLog).noquote()
        << QString("Bound %1 MCP tool(s) into ToolsManager").arg(m_currentlyRegistered.size());
}

} // namespace LLMCore::Mcp
