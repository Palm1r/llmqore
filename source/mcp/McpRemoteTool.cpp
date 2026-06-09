// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpRemoteTool.hpp>

#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpExceptions.hpp>

#include <LLMQore/FutureUtils.hpp>
#include <QPromise>

namespace LLMQore::Mcp {

McpRemoteTool::McpRemoteTool(McpClient *client, ToolInfo info, QObject *parent)
    : LLMQore::BaseTool(parent)
    , m_client(client)
    , m_info(std::move(info))
{}

McpRemoteTool::McpRemoteTool(
    McpClient *client, const QString &serverName, ToolInfo info, QObject *parent)
    : LLMQore::BaseTool(parent)
    , m_client(client)
    , m_serverName(serverName)
    , m_info(std::move(info))
{}

QString McpRemoteTool::id() const
{
    return m_serverName.isEmpty()
               ? m_info.name
               : QStringLiteral("%1_%2").arg(m_serverName, m_info.name);
}

QString McpRemoteTool::displayName() const
{
    return m_info.title.isEmpty() ? m_info.name : m_info.title;
}

QString McpRemoteTool::description() const
{
    return m_info.description;
}

QJsonObject McpRemoteTool::parametersSchema() const
{
    return m_info.inputSchema;
}

QFuture<LLMQore::ToolResult> McpRemoteTool::executeAsync(const QJsonObject &input)
{
    if (!m_client) {
        QPromise<LLMQore::ToolResult> p;
        p.start();
        p.addResult(LLMQore::ToolResult::error(
            QStringLiteral("MCP client is not available")));
        p.finish();
        return p.future();
    }

    return LLMQore::compat(m_client->callTool(m_info.name, input))
        .then(this, [](const LLMQore::ToolResult &result) { return result; })
        .onFailed(this, [](const auto &e) {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, McpException>)
                return LLMQore::ToolResult::error(e.message());
            return LLMQore::ToolResult::error(QString::fromUtf8(e.what()));
        });
}

} // namespace LLMQore::Mcp
