// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ToolHandler.hpp"
#include <LLMCore/ToolExceptions.hpp>

#include <QtConcurrent>

#include <LLMCore/Log.hpp>

namespace LLMCore {

ToolHandler::ToolHandler(QObject *parent)
    : QObject(parent)
{}

QFuture<QString> ToolHandler::executeToolAsync(
    const QString &requestId, const QString &toolId, BaseTool *tool, const QJsonObject &input)
{
    if (!tool) {
        QTimer::singleShot(0, this, [this, requestId, toolId]() {
            emit toolFailed(requestId, toolId, QStringLiteral("Tool is null"));
        });
        return QFuture<QString>();
    }

    auto *execution = new ToolExecution();
    execution->requestId = requestId;
    execution->toolId = toolId;
    execution->toolName = tool->id();
    execution->watcher = new QFutureWatcher<QString>(this);

    connect(execution->watcher, &QFutureWatcher<QString>::finished, this, [this, toolId]() {
        onToolExecutionFinished(toolId);
    });

    qCDebug(llmToolsLog).noquote()
        << QString("Starting tool execution: %1 (ID: %2)").arg(tool->id(), toolId);

    // FIX: Insert into map BEFORE setting the future on the watcher.
    // If the future is already completed (synchronous tool), the finished
    // signal fires immediately inside setFuture(). Without prior insertion,
    // onToolExecutionFinished would not find the execution in the map.
    m_activeExecutions.insert(toolId, execution);

    auto future = tool->executeAsync(input);
    execution->watcher->setFuture(future);

    return future;
}

void ToolHandler::cleanupRequest(const QString &requestId)
{
    auto it = m_activeExecutions.begin();
    while (it != m_activeExecutions.end()) {
        if (it.value()->requestId == requestId) {
            auto *execution = it.value();
            qCDebug(llmToolsLog).noquote()
                << QString("Canceling tool %1 for request %2").arg(execution->toolName, requestId);

            if (execution->watcher) {
                execution->watcher->disconnect();
                execution->watcher->cancel();
                execution->watcher->deleteLater();
            }
            delete execution;
            it = m_activeExecutions.erase(it);
        } else {
            ++it;
        }
    }
}

void ToolHandler::onToolExecutionFinished(const QString &toolId)
{
    if (!m_activeExecutions.contains(toolId)) {
        return;
    }

    auto *execution = m_activeExecutions.take(toolId);

    try {
        QString result = execution->watcher->result();
        qCDebug(llmToolsLog).noquote() << QString("Tool %1 completed").arg(execution->toolName);
        emit toolCompleted(execution->requestId, execution->toolId, result);
    } catch (const ToolException &e) {
        QString error = e.message();
        if (error.isEmpty())
            error = QStringLiteral("Tool execution failed with empty error message");
        qCWarning(llmToolsLog).noquote()
            << QString("Tool %1 failed: %2").arg(execution->toolName, error);
        emit toolFailed(execution->requestId, execution->toolId, error);
    } catch (const std::exception &e) {
        QString error = QString::fromStdString(e.what());
        if (error.isEmpty())
            error = QStringLiteral("Unknown exception occurred");
        qCWarning(llmToolsLog).noquote()
            << QString("Tool %1 failed: %2").arg(execution->toolName, error);
        emit toolFailed(execution->requestId, execution->toolId, error);
    } catch (...) {
        const QString error = QStringLiteral("Unknown error occurred during tool execution");
        qCWarning(llmToolsLog).noquote()
            << QString("Tool %1 failed: %2").arg(execution->toolName, error);
        emit toolFailed(execution->requestId, execution->toolId, error);
    }

    // Watcher is a QObject child of this, schedule for deletion.
    // Execution struct owns nothing else, safe to delete immediately.
    execution->watcher->deleteLater();
    execution->watcher = nullptr;
    delete execution;
}

} // namespace LLMCore
