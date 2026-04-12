// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpRemoteTool.hpp>

#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpExceptions.hpp>

#include <QPromise>

namespace LLMCore::Mcp {

McpRemoteTool::McpRemoteTool(McpClient *client, ToolInfo info, QObject *parent)
    : LLMCore::BaseTool(parent)
    , m_client(client)
    , m_info(std::move(info))
{}

QString McpRemoteTool::id() const
{
    return m_info.name;
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

QFuture<LLMCore::ToolResult> McpRemoteTool::executeAsync(const QJsonObject &input)
{
    if (!m_client) {
        QPromise<LLMCore::ToolResult> p;
        p.start();
        p.addResult(LLMCore::ToolResult::error(
            QStringLiteral("MCP client is not available")));
        p.finish();
        return p.future();
    }

    return m_client->callTool(m_info.name, input)
        .onFailed(this, [](const McpException &e) {
            return LLMCore::ToolResult::error(e.message());
        })
        .onFailed(this, [](const std::exception &e) {
            return LLMCore::ToolResult::error(QString::fromUtf8(e.what()));
        });
}

} // namespace LLMCore::Mcp
