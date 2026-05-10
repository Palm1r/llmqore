// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpTypes.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT McpRemoteTool : public LLMQore::BaseTool
{
    Q_OBJECT
public:
    McpRemoteTool(McpClient *client, ToolInfo info, QObject *parent = nullptr);

    McpRemoteTool(
        McpClient *client,
        const QString &serverName,
        ToolInfo info,
        QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QString description() const override;
    QJsonObject parametersSchema() const override;

    QFuture<LLMQore::ToolResult> executeAsync(
        const QJsonObject &input = QJsonObject()) override;

    const ToolInfo &info() const { return m_info; }
    const QString &serverName() const { return m_serverName; }

private:
    QPointer<McpClient> m_client;
    QString m_serverName;
    ToolInfo m_info;
};

} // namespace LLMQore::Mcp
