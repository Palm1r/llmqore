// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <LLMCore/LLMCore_global.h>

#include <LLMCore/BaseTool.hpp>
#include <LLMCore/ToolResult.hpp>

namespace LLMCore {

class LLMCORE_EXPORT ToolHandler : public QObject
{
    Q_OBJECT

public:
    explicit ToolHandler(QObject *parent = nullptr);

    QFuture<ToolResult> executeToolAsync(
        const QString &requestId, const QString &toolId, BaseTool *tool, const QJsonObject &input);

    void cleanupRequest(const QString &requestId);

signals:
    void toolCompleted(
        const QString &requestId, const QString &toolId, const LLMCore::ToolResult &result);
    void toolFailed(const QString &requestId, const QString &toolId, const QString &error);

private:
    struct ToolExecution
    {
        QString requestId;
        QString toolId;
        QString toolName;
        QFutureWatcher<ToolResult> *watcher;
    };

    QHash<QString, ToolExecution *> m_activeExecutions;

    void onToolExecutionFinished(const QString &toolId);
};

} // namespace LLMCore
