// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "LlmChatController.hpp"
#include "ExampleTools.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QUrl>
#include <QtDebug>

#include <LLMQore/Clients>
#include <LLMQore/McpHttpServerTransport.hpp>
#include <LLMQore/McpServer.hpp>
#include <LLMQore/Tools>

namespace {

LLMQore::BaseClient *makeClient(
    const QString &provider,
    const QString &url,
    const QString &apiKey,
    QObject *parent)
{
    if (provider == "Claude")
        return new LLMQore::ClaudeClient(url, apiKey, {}, nullptr, parent);
    if (provider == "OpenAI")
        return new LLMQore::OpenAIClient(url, apiKey, {}, nullptr, parent);
    if (provider == "OpenAI Responses")
        return new LLMQore::OpenAIResponsesClient(url, apiKey, {}, nullptr, parent);
    if (provider == "DeepSeek")
        return new LLMQore::OpenAIClient(url, apiKey, {}, nullptr, parent);
    if (provider == "Mistral") {
        auto *mistral = new LLMQore::OpenAIClient(url, apiKey, {}, nullptr, parent);
        mistral->setProfile(LLMQore::mistralProfile());
        return mistral;
    }
    if (provider == "Ollama")
        return new LLMQore::OllamaClient(url, apiKey, {}, nullptr, parent);
    if (provider == "Google AI")
        return new LLMQore::GoogleAIClient(url, apiKey, {}, nullptr, parent);
    if (provider == "LlamaCpp")
        return new LLMQore::LlamaCppClient(url, apiKey, {}, nullptr, parent);
    return nullptr;
}

} // namespace

LlmChatController::LlmChatController(QObject *parent)
    : ChatSession(parent)
{}

LlmChatController::~LlmChatController() = default;

bool LlmChatController::connectTo(
    const QString &provider, const QString &url, const QString &apiKey)
{
    cancelPendingFetch();

    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }

    m_client = makeClient(provider, url, apiKey, this);
    if (!m_client) {
        setStatus(QString("Unknown provider: %1").arg(provider));
        return false;
    }

    registerTools();
    wireClient();
    fetchModels();
    return true;
}

void LlmChatController::wireClient()
{
    connect(
        m_client->tools(), &LLMQore::ToolRegistry::toolsChanged,
        this, &LlmChatController::refreshToolNames);

    connect(m_client, &LLMQore::BaseClient::chunkReceived, this,
            [this](const LLMQore::RequestID &, const QString &chunk) {
                m_messages.appendOrCreate("assistant", chunk);
            });

    connect(m_client, &LLMQore::BaseClient::toolStarted, this,
            [this](const LLMQore::RequestID &, const QString &, const QString &toolName) {
                setStatus(QString("Tool: %1 ...").arg(toolName));
            });

    connect(m_client, &LLMQore::BaseClient::toolResultReady, this,
            [this](const LLMQore::RequestID &,
                   const QString &,
                   const QString &toolName,
                   const QString &result) {
                m_messages.append("tool", QString("[%1]: %2").arg(toolName, result));
            });

    connect(m_client, &LLMQore::BaseClient::requestFinalized, this,
            [this](const LLMQore::RequestID &, const LLMQore::CompletionInfo &info) {
                m_conversation = info.conversation;
            });

    connect(m_client, &LLMQore::BaseClient::requestCompleted, this,
            [this](const LLMQore::RequestID &, const QString &) {
                setBusy(false);
                setStatus("Ready");
            });

    connect(m_client, &LLMQore::BaseClient::requestFailed, this,
            [this](const LLMQore::RequestID &, const QString &error) {
                m_messages.append("error", error);
                setBusy(false);
                setStatus("Request failed");
            });
}

void LlmChatController::send(const QString &text, const QString &model)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || busy() || !m_client)
        return;

    m_client->setModel(model);
    m_messages.append("user", trimmed);
    setBusy(true);
    setStatus("Waiting for response...");

    LLMQore::Conversation pending = m_conversation;
    pending.addUser(trimmed);
    m_currentRequest = m_client->ask(pending);
}

void LlmChatController::stop()
{
    if (!busy() || !m_client || m_currentRequest.isEmpty())
        return;

    m_client->cancelRequest(m_currentRequest);
    m_currentRequest.clear();
}

void LlmChatController::clear()
{
    ChatSession::clear();
    m_conversation.clear();
}

void LlmChatController::registerTools()
{
    auto *tools = m_client->tools();

    tools->addTool(new Example::DateTimeTool);
    tools->addTool(new Example::CalculatorTool);
    tools->addTool(new Example::SystemInfoTool);

    const QString mcpConfigPath = qEnvironmentVariable("LLMQORE_MCP_CONFIG");
    if (!mcpConfigPath.isEmpty()) {
        QFile file(mcpConfigPath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning().noquote()
                << "LlmChatController: cannot open" << mcpConfigPath << ":" << file.errorString();
        } else {
            QJsonParseError error = {};
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                qWarning().noquote() << "LlmChatController: cannot parse" << mcpConfigPath << ":"
                                     << error.errorString();
            } else {
                const int loaded = tools->loadMcpServers(doc.object());
                qInfo().noquote() << "LlmChatController: loaded" << loaded << "MCP server(s)";
            }
        }
    }

    refreshToolNames();
}

void LlmChatController::exposeToolsOverHttp(quint16 port)
{
    if (m_toolServer || !m_client)
        return;

    LLMQore::Mcp::HttpServerConfig httpConfig;
    httpConfig.port = port;
    httpConfig.path = QStringLiteral("/mcp");

    LLMQore::Mcp::McpServerConfig serverConfig;
    serverConfig.serverInfo = {"llmqore-example-chat", "1.0.0"};
    serverConfig.instructions = "Tools of the running example-chat application";

    m_toolServer = new LLMQore::Mcp::McpServer(
        new LLMQore::Mcp::McpHttpServerTransport(httpConfig, this), serverConfig, this);

    m_toolServer->setToolRegistry(m_client->tools());
    m_toolServer->start();

    qInfo().noquote()
        << QString("Example tools exposed at http://127.0.0.1:%1/mcp").arg(port);
}

void LlmChatController::refreshToolNames()
{
    QStringList names;
    if (m_client) {
        for (const auto &snapshot : m_client->tools()->toolsSnapshot())
            names.append(QString("%1 - %2").arg(snapshot.displayName, snapshot.description));
    }
    setToolNames(names);
}

void LlmChatController::fetchModels()
{
    cancelPendingFetch();

    setModelList({});
    setLoadingModels(true);
    setStatus("Fetching models...");

    auto *watcher = new QFutureWatcher<QList<LLMQore::ModelInfo>>(this);
    m_modelWatcher = watcher;

    connect(watcher, &QFutureWatcher<QList<LLMQore::ModelInfo>>::finished, this, [this, watcher]() {
        if (m_modelWatcher != watcher)
            return;
        m_modelWatcher = nullptr;

        QStringList ids;
        if (!watcher->isCanceled() && watcher->future().resultCount() > 0) {
            for (const LLMQore::ModelInfo &info : watcher->result())
                ids.append(info.id);
        }

        setModelList(ids);
        setLoadingModels(false);
        setStatus(ids.isEmpty() ? "No models found" : QString("Loaded %1 models").arg(ids.size()));
        watcher->deleteLater();
    });

    watcher->setFuture(m_client->listModels());
}

void LlmChatController::cancelPendingFetch()
{
    if (!m_modelWatcher)
        return;

    m_modelWatcher->disconnect(this);
    m_modelWatcher->cancel();
    m_modelWatcher->deleteLater();
    m_modelWatcher = nullptr;
    setLoadingModels(false);
}
