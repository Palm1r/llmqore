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

namespace LLMCore {

class LLMCORE_EXPORT ToolHandler : public QObject
{
    Q_OBJECT

public:
    explicit ToolHandler(QObject *parent = nullptr);

    QFuture<QString> executeToolAsync(
        const QString &requestId, const QString &toolId, BaseTool *tool, const QJsonObject &input);

    void cleanupRequest(const QString &requestId);

signals:
    void toolCompleted(const QString &requestId, const QString &toolId, const QString &result);
    void toolFailed(const QString &requestId, const QString &toolId, const QString &error);

private:
    struct ToolExecution
    {
        QString requestId;
        QString toolId;
        QString toolName;
        QFutureWatcher<QString> *watcher;
    };

    QHash<QString, ToolExecution *> m_activeExecutions; // toolId -> execution

    void onToolExecutionFinished(const QString &toolId);
};

} // namespace LLMCore
