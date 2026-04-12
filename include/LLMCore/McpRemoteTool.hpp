// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <LLMCore/BaseTool.hpp>
#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpTypes.hpp>
#include <LLMCore/ToolResult.hpp>

namespace LLMCore::Mcp {

class LLMCORE_EXPORT McpRemoteTool : public LLMCore::BaseTool
{
    Q_OBJECT
public:
    McpRemoteTool(McpClient *client, ToolInfo info, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    QString description() const override;
    QJsonObject parametersSchema() const override;

    QFuture<LLMCore::ToolResult> executeAsync(
        const QJsonObject &input = QJsonObject()) override;

    const ToolInfo &info() const { return m_info; }

private:
    QPointer<McpClient> m_client;
    ToolInfo m_info;
};

} // namespace LLMCore::Mcp
