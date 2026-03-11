// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <LLMCore/BaseTool.hpp>
#include <LLMCore/ToolSchemaFormat.hpp>

namespace LLMCore {

class ToolHandler;

struct PendingTool
{
    QString id;
    QString name;
    QJsonObject input;
    QString result;
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
    void toolExecutionComplete(const QString &requestId, const QHash<QString, QString> &toolResults);

private slots:
    void onToolFinished(
        const QString &requestId, const QString &toolId, const QString &result, bool success);

private:
    void initConnections();
    void executeNextTool(const QString &requestId);
    QHash<QString, QString> getToolResults(const QString &requestId) const;
    QJsonArray buildToolsDefinitions() const;
    QJsonObject wrapDefinition(const BaseTool *tool) const;

    ToolHandler *m_toolHandler;
    ToolSchemaFormat m_format;
    QHash<QString, ToolQueue> m_toolQueues;
    int m_toolExecutionDelayMs = 0;

    QHash<QString, BaseTool *> m_tools;
};

} // namespace LLMCore
