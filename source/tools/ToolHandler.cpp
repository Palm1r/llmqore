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

QFuture<ToolResult> ToolHandler::executeToolAsync(
    const QString &requestId, const QString &toolId, BaseTool *tool, const QJsonObject &input)
{
    if (!tool) {
        QTimer::singleShot(0, this, [this, requestId, toolId]() {
            emit toolFailed(requestId, toolId, QStringLiteral("Tool is null"));
        });
        return QFuture<ToolResult>();
    }

    auto *execution = new ToolExecution();
    execution->requestId = requestId;
    execution->toolId = toolId;
    execution->toolName = tool->id();
    execution->watcher = new QFutureWatcher<ToolResult>(this);

    connect(execution->watcher, &QFutureWatcher<ToolResult>::finished, this, [this, toolId]() {
        onToolExecutionFinished(toolId);
    });

    qCDebug(llmToolsLog).noquote()
        << QString("Starting tool execution: %1 (ID: %2)").arg(tool->id(), toolId);

    // FIX: Insert before setFuture() — if the future is already complete,
    // finished fires synchronously and needs the map entry to exist.
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
        ToolResult result = execution->watcher->result();
        if (result.isError) {
            QString errorText = result.asText();
            if (errorText.isEmpty())
                errorText = QStringLiteral("Tool reported an error");
            qCWarning(llmToolsLog).noquote()
                << QString("Tool %1 reported isError: %2")
                       .arg(execution->toolName, errorText);
            emit toolFailed(execution->requestId, execution->toolId, errorText);
        } else {
            qCDebug(llmToolsLog).noquote()
                << QString("Tool %1 completed (%2 content blocks)")
                       .arg(execution->toolName)
                       .arg(result.content.size());
            emit toolCompleted(execution->requestId, execution->toolId, result);
        }
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

    execution->watcher->deleteLater();
    execution->watcher = nullptr;
    delete execution;
}

} // namespace LLMCore
