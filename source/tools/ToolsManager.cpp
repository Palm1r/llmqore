// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ToolHandler.hpp"
#include <LLMCore/Log.hpp>
#include <LLMCore/ToolsManager.hpp>
#include <QTimer>

namespace LLMCore {

ToolsManager::ToolsManager(ToolSchemaFormat format, QObject *parent)
    : QObject(parent)
    , m_toolHandler(new ToolHandler(this))
    , m_format(format)
{
    initConnections();
}

void ToolsManager::initConnections()
{
    connect(m_toolHandler, &ToolHandler::toolCompleted, this, &ToolsManager::onToolCompleted);
    connect(m_toolHandler, &ToolHandler::toolFailed, this, &ToolsManager::onToolErrored);
}

void ToolsManager::addTool(BaseTool *tool)
{
    if (!tool) {
        qCWarning(llmToolsLog).noquote() << "Attempted to add null tool";
        return;
    }

    const QString toolName = tool->id();
    if (m_tools.contains(toolName)) {
        qCDebug(llmToolsLog).noquote()
            << QString("Tool '%1' already registered, replacing").arg(toolName);
    }

    tool->setParent(this);
    m_tools.insert(toolName, tool);
    qCDebug(llmToolsLog).noquote() << QString("Added tool '%1'").arg(toolName);
}

void ToolsManager::removeTool(const QString &name)
{
    if (auto *t = m_tools.take(name)) {
        t->deleteLater();
        qCDebug(llmToolsLog).noquote() << QString("Removed tool '%1'").arg(name);
    }
}

BaseTool *ToolsManager::tool(const QString &name) const
{
    return m_tools.value(name, nullptr);
}

QList<BaseTool *> ToolsManager::registeredTools() const
{
    return m_tools.values();
}

QString ToolsManager::displayName(const QString &toolName) const
{
    if (auto *t = m_tools.value(toolName)) {
        return t->displayName();
    }
    return QStringLiteral("Unknown tool");
}

void ToolsManager::executeToolCall(
    const QString &requestId,
    const QString &toolId,
    const QString &toolName,
    const QJsonObject &input)
{
    qCDebug(llmToolsLog).noquote()
        << QString("Queueing tool %1 (ID: %2) for request %3").arg(toolName, toolId, requestId);

    if (!m_toolQueues.contains(requestId)) {
        m_toolQueues[requestId] = ToolQueue();
    }

    auto &queue = m_toolQueues[requestId];

    for (const auto &tool : queue.queue) {
        if (tool.id == toolId) {
            qCDebug(llmToolsLog).noquote()
                << QString("Tool %1 already in queue for request %2").arg(toolId, requestId);
            return;
        }
    }

    if (queue.completed.contains(toolId)) {
        qCDebug(llmToolsLog).noquote()
            << QString("Tool %1 already completed for request %2").arg(toolId, requestId);
        return;
    }

    PendingTool pendingTool;
    pendingTool.id = toolId;
    pendingTool.name = toolName;
    pendingTool.input = input;
    queue.queue.append(pendingTool);

    qCDebug(llmToolsLog).noquote()
        << QString("Tool %1 added to queue (position %2)").arg(toolName).arg(queue.queue.size());

    if (!queue.isExecuting) {
        executeNextTool(requestId);
    }
}

void ToolsManager::executeNextTool(const QString &requestId)
{
    if (!m_toolQueues.contains(requestId)) {
        return;
    }

    auto &queue = m_toolQueues[requestId];

    while (!queue.queue.isEmpty()) {
        PendingTool pendingTool = queue.queue.takeFirst();
        queue.isExecuting = true;

        BaseTool *toolInstance = m_tools.value(pendingTool.name);
        if (!toolInstance) {
            qCWarning(llmToolsLog).noquote()
                << QString("Tool not found: %1").arg(pendingTool.name);
            const QString errText
                = QString("Error: Tool not found: %1").arg(pendingTool.name);
            pendingTool.result = ToolResult::error(errText);
            pendingTool.resultText = errText;
            pendingTool.complete = true;
            queue.completed[pendingTool.id] = pendingTool;
            continue;
        }

        // Insert into completed with complete=false; finalizePendingTool() will
        // set complete=true once the async execution finishes.  The entry must
        // exist before executeToolAsync() because a synchronously-resolved
        // future triggers finalizePendingTool() immediately.
        pendingTool.complete = false;
        queue.completed[pendingTool.id] = pendingTool;

        qCDebug(llmToolsLog).noquote()
            << QString("Executing tool %1 (ID: %2) for request %3 (%4 remaining)")
                   .arg(pendingTool.name, pendingTool.id, requestId)
                   .arg(queue.queue.size());

        emit toolExecutionStarted(requestId, pendingTool.id, pendingTool.name);

        m_toolHandler->executeToolAsync(requestId, pendingTool.id, toolInstance, pendingTool.input);
        qCDebug(llmToolsLog).noquote()
            << QString("Started async execution of %1").arg(pendingTool.name);
        return;
    }

    qCDebug(llmToolsLog).noquote()
        << QString("All tools complete for request %1, emitting results").arg(requestId);
    QHash<QString, ToolResult> results = getToolResults(requestId);
    emit toolExecutionComplete(requestId, results);
    queue.isExecuting = false;
}

QJsonArray ToolsManager::getToolsDefinitions() const
{
    return buildToolsDefinitions();
}

QJsonObject ToolsManager::wrapDefinition(const BaseTool *tool) const
{
    QJsonObject schema = tool->parametersSchema();

    switch (m_format) {
    case ToolSchemaFormat::OpenAI:
    case ToolSchemaFormat::Ollama: {
        QJsonObject function;
        function["name"] = tool->id();
        function["description"] = tool->description();
        function["parameters"] = schema;

        QJsonObject wrapped;
        wrapped["type"] = "function";
        wrapped["function"] = function;
        return wrapped;
    }
    case ToolSchemaFormat::OpenAIResponses: {
        QJsonObject wrapped;
        wrapped["type"] = "function";
        wrapped["name"] = tool->id();
        wrapped["description"] = tool->description();
        wrapped["parameters"] = schema;
        return wrapped;
    }
    case ToolSchemaFormat::Claude: {
        QJsonObject wrapped;
        wrapped["name"] = tool->id();
        wrapped["description"] = tool->description();
        wrapped["input_schema"] = schema;
        return wrapped;
    }
    case ToolSchemaFormat::Google: {
        QJsonObject functionDeclaration;
        functionDeclaration["name"] = tool->id();
        functionDeclaration["description"] = tool->description();
        functionDeclaration["parameters"] = schema;
        return functionDeclaration;
    }
    }

    Q_UNREACHABLE();
    return {};
}

QJsonArray ToolsManager::buildToolsDefinitions() const
{
    QJsonArray toolsArray;

    for (auto it = m_tools.constBegin(); it != m_tools.constEnd(); ++it) {
        BaseTool *t = it.value();
        if (!t || !t->isEnabled()) {
            continue;
        }

        toolsArray.append(wrapDefinition(t));
    }

    if (m_format == ToolSchemaFormat::Google && !toolsArray.isEmpty()) {
        QJsonArray functionDeclarations;
        for (const auto &item : toolsArray)
            functionDeclarations.append(item);

        QJsonObject wrapper;
        wrapper["function_declarations"] = functionDeclarations;

        return QJsonArray{wrapper};
    }

    return toolsArray;
}

void ToolsManager::cleanupRequest(const QString &requestId)
{
    if (m_toolQueues.contains(requestId)) {
        m_toolHandler->cleanupRequest(requestId);
        m_toolQueues.remove(requestId);
    }
}

void ToolsManager::onToolCompleted(
    const QString &requestId, const QString &toolId, const ToolResult &result)
{
    finalizePendingTool(requestId, toolId, result, /*success*/ true);
}

void ToolsManager::onToolErrored(
    const QString &requestId, const QString &toolId, const QString &errorText)
{
    finalizePendingTool(
        requestId, toolId, ToolResult::error(errorText), /*success*/ false);
}

void ToolsManager::finalizePendingTool(
    const QString &requestId, const QString &toolId, const ToolResult &rich, bool success)
{
    if (!m_toolQueues.contains(requestId))
        return;

    auto &queue = m_toolQueues[requestId];
    if (!queue.completed.contains(toolId))
        return;

    PendingTool &pendingTool = queue.completed[toolId];
    pendingTool.result = rich;
    pendingTool.resultText
        = success ? rich.asText() : QString("Error: %1").arg(rich.asText());
    pendingTool.complete = true;

    qCDebug(llmToolsLog).noquote() << QString("Tool %1 %2 for request %3")
                                          .arg(toolId)
                                          .arg(success ? QString("completed") : QString("failed"))
                                          .arg(requestId);

    emit toolExecutionResult(requestId, toolId, pendingTool.name, pendingTool.resultText);

    if (m_toolExecutionDelayMs > 0 && !queue.queue.isEmpty()) {
        QTimer::singleShot(m_toolExecutionDelayMs, this, [this, requestId]() {
            executeNextTool(requestId);
        });
    } else {
        executeNextTool(requestId);
    }
}

QHash<QString, ToolResult> ToolsManager::getToolResults(const QString &requestId) const
{
    QHash<QString, ToolResult> results;

    if (m_toolQueues.contains(requestId)) {
        const auto &queue = m_toolQueues[requestId];
        for (auto it = queue.completed.begin(); it != queue.completed.end(); ++it) {
            if (it.value().complete)
                results[it.key()] = it.value().result;
        }
    }

    return results;
}

void ToolsManager::setToolExecutionDelay(int delayMs)
{
    m_toolExecutionDelayMs = delayMs;
}

int ToolsManager::toolExecutionDelay() const
{
    return m_toolExecutionDelayMs;
}

} // namespace LLMCore
