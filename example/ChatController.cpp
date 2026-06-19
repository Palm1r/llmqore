// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ChatController.hpp"
#include "ExampleTools.hpp"

#include <optional>

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrl>
#include <QtDebug>

#include <LLMQore/Clients>
#include <LLMQore/Tools>

#include <LLMQore/AcpAgentConfig.hpp>
#include <LLMQore/AcpClient.hpp>
#include <LLMQore/CallbackPermissionProvider.hpp>
#include <LLMQore/DefaultFileSystemProvider.hpp>
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/TerminalManager.hpp>

namespace Acp = LLMQore::Acp;

namespace {

std::optional<Acp::AcpAgentConfig> acpConfigFor(
    const Acp::AcpAgentRegistry &registry, const QString &provider, const QString &cwd)
{
    for (const Acp::AcpAgentEntry &e : registry.entries()) {
        if (e.name == provider)
            return registry.config(e.id, cwd);
    }
    return std::nullopt;
}

QString interactivePath()
{
    static const QString cached = [] {
        const QString shell = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
        QProcess p;
        p.start(shell, {QStringLiteral("-lc"), QStringLiteral("printf %s \"$PATH\"")});
        if (p.waitForStarted(2000) && p.waitForFinished(3000)) {
            const QString out = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
            if (!out.isEmpty())
                return out;
        }
        return qEnvironmentVariable("PATH");
    }();
    return cached;
}

} // namespace

ChatController::ChatController(QObject *parent)
    : QObject(parent)
{
    const QString path
        = qEnvironmentVariable("LLMQORE_ACP_AGENTS", QStringLiteral(":/agents.json"));
    m_acpRegistry.loadFromFile(path);
}

QStringList ChatController::acpAgentNames() const
{
    QStringList names;
    for (const Acp::AcpAgentEntry &e : m_acpRegistry.entries())
        names.append(e.name);
    return names;
}

void ChatController::setupProvider(const QString &provider, const QString &url, const QString &apiKey)
{
    clearChat();
    if (acpConfigFor(m_acpRegistry, provider, QDir::currentPath())) {
        setupAcpAgent(provider);
        return;
    }
    createClient(provider, url, apiKey);
    fetchModels();
}

void ChatController::send(const QString &text, const QString &model)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || m_busy)
        return;

    if (m_acpMode) {
        if (!m_acpClient || m_acpSessionId.isEmpty()) {
            setStatus("Agent not ready yet");
            return;
        }
        m_messages.append("user", trimmed);
        setBusy(true);
        setStatus("Waiting for agent...");
        LLMQore::compat(
            m_acpClient->prompt(m_acpSessionId, {Acp::ContentBlock::makeText(trimmed)}))
            .onFailed(this, [this](const std::exception &e) -> Acp::PromptResult {
                m_messages.append("error", QString::fromUtf8(e.what()));
                setBusy(false);
                setStatus("Request failed");
                return Acp::PromptResult{};
            });
        return;
    }

    if (!m_client)
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
        payload["max_tokens"] = 20000;

    QJsonArray toolsDefs = m_client->tools()->getToolsDefinitions();
    if (!toolsDefs.isEmpty())
        payload["tools"] = toolsDefs;

    m_currentRequest = m_client->sendMessage(payload);
}

void ChatController::stopGeneration()
{
    if (!m_busy)
        return;

    if (m_acpMode) {
        if (m_acpClient && !m_acpSessionId.isEmpty())
            m_acpClient->cancel(m_acpSessionId);
        return;
    }

    if (!m_client || m_currentRequest.isEmpty())
        return;

    m_client->cancelRequest(m_currentRequest);
    m_currentRequest.clear();
}

QString ChatController::envApiKey(const QString &provider) const
{
    static const QHash<QString, QString> envMap = {
        {"Claude",    "CLAUDE_API_KEY"},
        {"OpenAI",    "OPENAI_API_KEY"},
        {"Google AI", "GOOGLE_API_KEY"},
    };
    const QString var = envMap.value(provider);
    if (var.isEmpty())
        return {};
    return QProcessEnvironment::systemEnvironment().value(var);
}

void ChatController::clearChat()
{
    m_messages.clear();
    m_history = QJsonArray();
}

void ChatController::cancelPendingFetch()
{
    if (m_modelWatcher) {
        m_modelWatcher->disconnect(this);
        m_modelWatcher->cancel();
        m_modelWatcher->deleteLater();
        m_modelWatcher = nullptr;
    }
    setLoadingModels(false);
}

void ChatController::createClient(const QString &provider, const QString &url, const QString &apiKey)
{
    teardownClients();
    m_currentProvider = provider;

    if (provider == "Claude")
        m_client = new LLMQore::ClaudeClient(url, apiKey, QString(), this);
    else if (provider == "OpenAI")
        m_client = new LLMQore::OpenAIClient(url, apiKey, QString(), this);
    else if (provider == "Ollama")
        m_client = new LLMQore::OllamaClient(url, apiKey, QString(), this);
    else if (provider == "Google AI")
        m_client = new LLMQore::GoogleAIClient(url, apiKey, QString(), this);
    else if (provider == "LlamaCpp")
        m_client = new LLMQore::LlamaCppClient(url, apiKey, QString(), this);

    if (!m_client)
        return;

    registerTools();

    connect(m_client->tools(), &LLMQore::ToolRegistry::toolsChanged,
            this, &ChatController::refreshToolListUi);

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
    connect(m_client, &LLMQore::BaseClient::requestCompleted, this,
            [this](const LLMQore::RequestID &, const QString &fullText) {
                m_history.append(QJsonObject{{"role", "assistant"}, {"content", fullText}});
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

void ChatController::teardownClients()
{
    cancelPendingFetch();
    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }
    if (m_acpClient) {
        m_acpClient->shutdown();
        m_acpClient->deleteLater();
        m_acpClient = nullptr;
    }
    m_acpSessionId.clear();
    m_acpMode = false;
}

void ChatController::setupAcpAgent(const QString &provider)
{
    teardownClients();
    m_currentProvider = provider;
    m_acpMode = true;

    const QString cwd = QDir::currentPath();
    Acp::AcpAgentConfig config = *acpConfigFor(m_acpRegistry, provider, cwd);

    const QString shellPath = interactivePath();
    const QString resolved = QStandardPaths::findExecutable(
        config.command, shellPath.split(QLatin1Char(':'), Qt::SkipEmptyParts));
    if (resolved.isEmpty()) {
        m_modelList.clear();
        emit modelListChanged();
        setStatus(QString("Agent '%1' not found on PATH").arg(config.command));
        return;
    }
    config.command = resolved;
    config.env.append(Acp::EnvVariable{QStringLiteral("PATH"), shellPath});

    auto *transport = config.createTransport(this);
    auto *acp = new Acp::AcpClient(transport, {}, this);
    m_acpClient = acp;

    acp->setFileSystemProvider(new Acp::DefaultFileSystemProvider(this));
    acp->setTerminalProvider(new Acp::TerminalManager(this));
    acp->setPermissionProvider(new Acp::CallbackPermissionProvider(
        [this](const QString &,
               const Acp::ToolCall &tc,
               const QList<Acp::PermissionOption> &opts) {
            m_messages.append("tool", QString("[permission] auto-allowed: %1").arg(tc.title));
            return opts.isEmpty() ? Acp::RequestPermissionResult::cancelled()
                                  : Acp::RequestPermissionResult::selected(opts.first().optionId);
        },
        this));

    connect(acp, &Acp::AcpClient::agentMessageChunk, this,
            [this](const QString &, const Acp::ContentBlock &c) {
                m_messages.appendOrCreate("assistant", c.text);
            });
    connect(acp, &Acp::AcpClient::agentThoughtChunk, this,
            [this](const QString &, const Acp::ContentBlock &c) {
                setStatus(QStringLiteral("Thinking: %1").arg(c.text.left(60)));
            });
    connect(acp, &Acp::AcpClient::toolCallStarted, this,
            [this](const QString &, const Acp::ToolCall &t) {
                m_messages.append(
                    "tool", QString("[%1] %2").arg(t.kind.isEmpty() ? QStringLiteral("tool") : t.kind, t.title));
            });
    connect(acp, &Acp::AcpClient::toolCallUpdated, this,
            [this](const QString &, const Acp::ToolCall &t) {
                if (!t.status.isEmpty())
                    setStatus(QString("Tool %1: %2").arg(t.title, t.status));
            });
    connect(acp, &Acp::AcpClient::promptFinished, this,
            [this](const QString &, const QString &stopReason) {
                setBusy(false);
                setStatus(QString("Ready (%1)").arg(stopReason));
            });
    connect(acp, &Acp::AcpClient::errorOccurred, this, [this](const QString &error) {
        m_messages.append("error", error);
        setBusy(false);
        setStatus("Error");
    });

    m_modelList = QStringList{provider};
    emit modelListChanged();
    setLoadingModels(false);
    m_toolNames.clear();
    emit toolNamesChanged();

    setBusy(true);
    setStatus(QString("Starting %1 ...").arg(config.command));

    LLMQore::compat(acp->connectAndInitialize())
        .then(acp,
              [acp, cwd](const Acp::InitializeResult &) -> QFuture<Acp::NewSessionResult> {
                  Acp::NewSessionParams params;
                  params.cwd = cwd;
                  return acp->newSession(params);
              })
        .unwrap()
        .then(this,
              [this, acp](const Acp::NewSessionResult &ns) -> int {
                  if (m_acpClient != acp)
                      return 0;
                  m_acpSessionId = ns.sessionId;
                  setBusy(false);
                  setStatus("ACP session ready");
                  return 0;
              })
        .onFailed(this, [this, acp](const std::exception &e) -> int {
            if (m_acpClient != acp)
                return 0;
            m_messages.append("error", QString::fromUtf8(e.what()));
            setBusy(false);
            setStatus("Failed to start agent");
            return 0;
        });
}

void ChatController::fetchModels()
{
    if (!m_client)
        return;

    cancelPendingFetch();

    m_modelList.clear();
    emit modelListChanged();
    setLoadingModels(true);
    setStatus("Fetching models...");

    auto *watcher = new QFutureWatcher<QList<QString>>(this);
    m_modelWatcher = watcher;

    connect(watcher, &QFutureWatcher<QList<QString>>::finished, this, [this, watcher]() {
        if (m_modelWatcher != watcher)
            return;
        m_modelWatcher = nullptr;

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

    tools->addTool(new Example::DateTimeTool);
    tools->addTool(new Example::CalculatorTool);
    tools->addTool(new Example::SystemInfoTool);

    const QString mcpConfigPath = QString::fromLocal8Bit(qgetenv("LLMQORE_MCP_CONFIG"));
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
    for (const auto &snap : m_client->tools()->toolsSnapshot())
        m_toolNames.append(QString("%1 - %2").arg(snap.displayName, snap.description));
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
