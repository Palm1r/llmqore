// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ToolHandler.hpp"
#include <LLMQore/Log.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpHttpTransport.hpp>
#include <LLMQore/McpRemoteTool.hpp>
#include <LLMQore/McpStdioTransport.hpp>
#include <LLMQore/ToolsManager.hpp>
#include <LLMQore/Version.hpp>
#include <QSet>
#include <QTimer>

namespace LLMQore {

namespace {

QJsonValue sanitizeSchemaValueForGoogle(const QJsonValue &value);

QJsonObject sanitizeSchemaForGoogle(const QJsonObject &schema)
{
    static const QSet<QString> kUnsupported{
        QStringLiteral("$schema"),
        QStringLiteral("$id"),
        QStringLiteral("$ref"),
        QStringLiteral("$defs"),
        QStringLiteral("definitions"),
        QStringLiteral("additionalProperties"),
        QStringLiteral("patternProperties"),
        QStringLiteral("unevaluatedProperties"),
        QStringLiteral("dependencies"),
        QStringLiteral("dependentSchemas"),
        QStringLiteral("dependentRequired"),
        QStringLiteral("allOf"),
        QStringLiteral("oneOf"),
        QStringLiteral("not"),
        QStringLiteral("const"),
    };

    QJsonObject result;
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        if (kUnsupported.contains(it.key()))
            continue;
        result.insert(it.key(), sanitizeSchemaValueForGoogle(it.value()));
    }
    return result;
}

QJsonValue sanitizeSchemaValueForGoogle(const QJsonValue &value)
{
    if (value.isObject())
        return sanitizeSchemaForGoogle(value.toObject());
    if (value.isArray()) {
        QJsonArray out;
        const QJsonArray arr = value.toArray();
        for (const auto &item : arr)
            out.append(sanitizeSchemaValueForGoogle(item));
        return out;
    }
    return value;
}

} // namespace

ToolsManager::ToolsManager(ToolSchemaFormat format, QObject *parent)
    : ToolRegistry(parent)
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

void ToolsManager::removeAllTools()
{
    ToolRegistry::removeAllTools();
    m_mcpClientTools.clear();
}

void ToolsManager::addMcpServer(const McpServerEntry &entry)
{
    Mcp::McpTransport *transport = nullptr;

    if (entry.url.isValid()) {
        Mcp::HttpTransportConfig cfg;
        cfg.endpoint = entry.url;
        cfg.headers = entry.headers;
        if (entry.httpSpec == QLatin1String("2024-11-05"))
            cfg.spec = Mcp::McpHttpSpec::V2024_11_05;
        transport = new Mcp::McpHttpTransport(cfg, nullptr, this);
    } else if (!entry.command.isEmpty()) {
        Mcp::StdioLaunchConfig cfg;
        cfg.program = entry.command;
        cfg.arguments = entry.arguments;
        cfg.environment = entry.env;
        cfg.workingDirectory = entry.workingDirectory;
        transport = new Mcp::McpStdioClientTransport(cfg, this);
    } else {
        qCWarning(llmToolsLog).noquote()
            << QString("McpServerEntry '%1': no command or url specified").arg(entry.name);
        return;
    }

    auto *client = new Mcp::McpClient(
        transport, Mcp::Implementation{entry.name, QStringLiteral(LLMQORE_VERSION_STRING)}, this);

    client->connectAndInitialize().then(this, [this, client](const Mcp::InitializeResult &) {
        addMcpClient(client);
    }).onFailed(this, [entry](const std::exception &e) {
        qCWarning(llmToolsLog).noquote()
            << QString("Failed to connect MCP server '%1': %2")
                   .arg(entry.name, QString::fromUtf8(e.what()));
    });
}

void ToolsManager::loadMcpServers(const QJsonObject &config)
{
    const QJsonObject servers = config["mcpServers"].toObject();
    for (auto it = servers.begin(); it != servers.end(); ++it) {
        const QJsonObject server = it.value().toObject();
        McpServerEntry entry;
        entry.name = it.key();

        if (server.contains("url")) {
            entry.url = QUrl(server["url"].toString());
            entry.httpSpec = server["spec"].toString();
            const QJsonObject headers = server["headers"].toObject();
            for (auto h = headers.begin(); h != headers.end(); ++h)
                entry.headers.insert(h.key(), h.value().toString());
        } else {
            entry.command = server["command"].toString();
            const QJsonArray args = server["args"].toArray();
            for (const auto &arg : args)
                entry.arguments.append(arg.toString());
            const QJsonObject envObj = server["env"].toObject();
            if (!envObj.isEmpty()) {
                for (auto e = envObj.begin(); e != envObj.end(); ++e)
                    entry.env.insert(e.key(), e.value().toString());
            }
            entry.workingDirectory = server["workingDirectory"].toString();
        }

        addMcpServer(entry);
    }
}

void ToolsManager::addMcpClient(Mcp::McpClient *client)
{
    if (!client) {
        qCWarning(llmToolsLog).noquote() << "Attempted to add null McpClient";
        return;
    }

    connect(client, &Mcp::McpClient::toolsChanged, this, [this, client]() {
        registerMcpTools(client);
    });

    connect(client, &QObject::destroyed, this, [this, client]() {
        removeMcpClient(client);
    });

    registerMcpTools(client);
}

void ToolsManager::removeMcpClient(Mcp::McpClient *client)
{
    const QStringList names = m_mcpClientTools.take(client);
    for (const QString &name : names)
        removeTool(name);
}

void ToolsManager::registerMcpTools(Mcp::McpClient *client)
{
    client->listTools().then(this, [this, client](const QList<Mcp::ToolInfo> &tools) {
        QSet<QString> incoming;
        incoming.reserve(tools.size());
        for (const Mcp::ToolInfo &t : tools)
            incoming.insert(t.name);

        const QStringList previous = m_mcpClientTools.value(client);
        for (const QString &old : previous) {
            if (!incoming.contains(old))
                removeTool(old);
        }

        QStringList registered;
        registered.reserve(tools.size());
        for (const Mcp::ToolInfo &info : tools) {
            if (m_tools.contains(info.name))
                removeTool(info.name);
            addTool(new Mcp::McpRemoteTool(client, info));
            registered.append(info.name);
        }
        m_mcpClientTools[client] = std::move(registered);
    });
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
        functionDeclaration["parameters"] = sanitizeSchemaForGoogle(schema);
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

} // namespace LLMQore
