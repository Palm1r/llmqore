// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ChatController.hpp"
#include "ExampleTools.hpp"

#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QtDebug>

#include <LLMCore/Clients>
#include <LLMCore/Tools>

ChatController::ChatController(QObject *parent)
    : QObject(parent)
{}

void ChatController::setupProvider(const QString &provider, const QString &url, const QString &apiKey)
{
    clearChat();
    createClient(provider, url, apiKey);
    fetchModels();
}

void ChatController::send(const QString &text, const QString &model)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || m_busy || !m_client)
        return;

    m_messages.append("user", trimmed);
    setBusy(true);
    setStatus("Waiting for response...");

    m_history.append(QJsonObject{{"role", "user"}, {"content", trimmed}});

    QJsonObject payload;
    payload["model"] = model;
    payload["stream"] = true;
    payload["messages"] = m_history;

    if (m_currentProvider == "Claude")
        payload["max_tokens"] = 4096;

    QJsonArray toolsDefs = m_client->tools()->getToolsDefinitions();
    if (!toolsDefs.isEmpty())
        payload["tools"] = toolsDefs;

    LLMCore::RequestCallbacks callbacks;

    callbacks.onChunk = [this](const LLMCore::RequestID &, const QString &chunk) {
        m_messages.appendOrCreate("assistant", chunk);
    };

    callbacks.onToolStarted =
        [this](const LLMCore::RequestID &, const QString &, const QString &toolName) {
            setStatus(QString("Tool: %1 ...").arg(toolName));
        };

    callbacks.onToolResult = [this](
                                 const LLMCore::RequestID &,
                                 const QString &,
                                 const QString &toolName,
                                 const QString &result) {
        m_messages.append("tool", QString("[%1]: %2").arg(toolName, result));
    };

    callbacks.onCompleted = [this](const LLMCore::RequestID &, const QString &fullText) {
        m_history.append(QJsonObject{{"role", "assistant"}, {"content", fullText}});
        setBusy(false);
        setStatus("Ready");
    };

    callbacks.onFailed = [this](const LLMCore::RequestID &, const QString &error) {
        m_messages.append("error", error);
        setBusy(false);
        setStatus("Request failed");
    };

    m_currentRequest = m_client->sendMessage(payload, callbacks);
}

void ChatController::stopGeneration()
{
    if (!m_busy || !m_client || m_currentRequest.isEmpty())
        return;

    m_client->cancelRequest(m_currentRequest);
    m_currentRequest.clear();
}

void ChatController::clearChat()
{
    m_messages.clear();
    m_history = QJsonArray();
}

void ChatController::createClient(const QString &provider, const QString &url, const QString &apiKey)
{
    m_currentProvider = provider;
    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }

    if (provider == "Claude")
        m_client = new LLMCore::ClaudeClient(url, apiKey, QString(), this);
    else if (provider == "OpenAI")
        m_client = new LLMCore::OpenAIClient(url, apiKey, QString(), this);
    else if (provider == "Ollama")
        m_client = new LLMCore::OllamaClient(url, apiKey, QString(), this);
    else if (provider == "Google AI")
        m_client = new LLMCore::GoogleAIClient(url, apiKey, QString(), this);
    else if (provider == "LlamaCpp")
        m_client = new LLMCore::LlamaCppClient(url, apiKey, QString(), this);

    if (!m_client)
        return;

    registerTools();

    connect(m_client->tools(), &LLMCore::ToolRegistry::toolsChanged,
            this, &ChatController::refreshToolListUi);
}

void ChatController::fetchModels()
{
    if (!m_client)
        return;

    m_modelList.clear();
    emit modelListChanged();
    setLoadingModels(true);
    setStatus("Fetching models...");

    auto *watcher = new QFutureWatcher<QList<QString>>(this);
    connect(watcher, &QFutureWatcher<QList<QString>>::finished, this, [this, watcher]() {
        m_modelList = QStringList(watcher->result().cbegin(), watcher->result().cend());
        emit modelListChanged();
        setLoadingModels(false);
        setStatus(
            m_modelList.isEmpty() ? "No models found"
                                  : QString("Loaded %1 models").arg(m_modelList.size()));
        watcher->deleteLater();
    });
    watcher->setFuture(m_client->listModels());
}

void ChatController::registerTools()
{
    if (!m_client)
        return;

    auto *tools = m_client->tools();

    // Built-in example tools.
    tools->addTool(new Example::DateTimeTool);
    tools->addTool(new Example::CalculatorTool);
    tools->addTool(new Example::SystemInfoTool);

    // MCP servers: from LLMCORE_MCP_CONFIG env var, or hardcoded fallback.
    const QString mcpConfigPath = QString::fromLocal8Bit(qgetenv("LLMCORE_MCP_CONFIG"));
    if (!mcpConfigPath.isEmpty()) {
        loadMcpConfig(mcpConfigPath);
    } else {
        tools->addMcpServer({
            .name = "qtcreator",
            .url = QUrl("http://127.0.0.1:3001/sse"),
            .httpSpec = "2024-11-05",
        });
    }

    refreshToolListUi();
}

void ChatController::refreshToolListUi()
{
    m_toolNames.clear();
    if (!m_client)
        return;
    for (auto *tool : m_client->tools()->registeredTools())
        m_toolNames.append(QString("%1 - %2").arg(tool->displayName(), tool->description()));
    emit toolNamesChanged();
}

void ChatController::loadMcpConfig(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "ChatController: cannot open" << path << ":" << f.errorString();
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning().noquote() << "ChatController: cannot parse" << path << ":" << err.errorString();
        return;
    }

    m_client->tools()->loadMcpServers(doc.object());
}

void ChatController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ChatController::setLoadingModels(bool loading)
{
    if (m_loadingModels == loading)
        return;
    m_loadingModels = loading;
    emit loadingModelsChanged();
}

void ChatController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
