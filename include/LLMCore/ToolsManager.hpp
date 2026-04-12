// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>

#include <LLMCore/BaseTool.hpp>
#include <LLMCore/ToolResult.hpp>
#include <LLMCore/ToolSchemaFormat.hpp>

namespace LLMCore {

class ToolHandler;

struct PendingTool
{
    QString id;
    QString name;
    QJsonObject input;
    ToolResult result;
    QString resultText;
    bool complete = false;
};

struct ToolQueue
{
    QList<PendingTool> queue;
    QHash<QString, PendingTool> completed;
    bool isExecuting = false;
};

class LLMCORE_EXPORT ToolsManager : public QObject
{
    Q_OBJECT

public:
    explicit ToolsManager(ToolSchemaFormat format, QObject *parent = nullptr);

    void addTool(BaseTool *tool);
    void removeTool(const QString &name);
    BaseTool *tool(const QString &name) const;
    QList<BaseTool *> registeredTools() const;

    QJsonArray getToolsDefinitions() const;
    QString displayName(const QString &toolName) const;

    void executeToolCall(
        const QString &requestId,
        const QString &toolId,
        const QString &toolName,
        const QJsonObject &input);
    void cleanupRequest(const QString &requestId);

    void setToolExecutionDelay(int delayMs);
    int toolExecutionDelay() const;

signals:
    void toolExecutionStarted(
        const QString &requestId, const QString &toolId, const QString &toolName);
    void toolExecutionResult(
        const QString &requestId,
        const QString &toolId,
        const QString &toolName,
        const QString &result);
    void toolExecutionComplete(
        const QString &requestId, const QHash<QString, ToolResult> &toolResults);

private slots:
    void onToolCompleted(
        const QString &requestId, const QString &toolId, const ToolResult &result);
    void onToolErrored(
        const QString &requestId, const QString &toolId, const QString &errorText);

private:
    void initConnections();
    void executeNextTool(const QString &requestId);
    void finalizePendingTool(
        const QString &requestId, const QString &toolId, const ToolResult &rich, bool success);
    QHash<QString, ToolResult> getToolResults(const QString &requestId) const;
    QJsonArray buildToolsDefinitions() const;
    QJsonObject wrapDefinition(const BaseTool *tool) const;

    ToolHandler *m_toolHandler;
    ToolSchemaFormat m_format;
    QHash<QString, ToolQueue> m_toolQueues;
    int m_toolExecutionDelayMs = 0;

    // QMap for deterministic alphabetical iteration order — important for
    // reproducible test output and stable round-trips on the wire.
    QMap<QString, BaseTool *> m_tools;
};

} // namespace LLMCore
