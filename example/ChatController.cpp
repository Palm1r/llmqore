// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ChatController.hpp"
#include "ExampleTools.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QtDebug>

#include <LLMCore/Clients>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpHttpTransport.hpp>
#include <LLMCore/McpRemoteTool.hpp>
#include <LLMCore/McpStdioTransport.hpp>
#include <LLMCore/McpTransport.hpp>
#include <LLMCore/Tools>

using LLMCore::Mcp::HttpTransportConfig;
using LLMCore::Mcp::Implementation;
using LLMCore::Mcp::InitializeResult;
using LLMCore::Mcp::McpClient;
using LLMCore::Mcp::McpHttpSpec;
using LLMCore::Mcp::McpHttpTransport;
using LLMCore::Mcp::McpRemoteTool;
using LLMCore::Mcp::McpStdioClientTransport;
using LLMCore::Mcp::McpTransport;
using LLMCore::Mcp::StdioLaunchConfig;
using LLMCore::Mcp::ToolInfo;

ChatController::ChatController(QObject *parent)
    : QObject(parent)
{
    loadMcpConfig();
}

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

    registerTools();
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

    // Drop whatever is currently registered so repeated calls (from provider
    // switches or from late-arriving MCP servers) produce a clean set and
    // not a flurry of "replacing" log entries.
    const QList<LLMCore::BaseTool *> existing = tools->registeredTools();
    QStringList existingNames;
    existingNames.reserve(existing.size());
    for (auto *t : existing)
        existingNames.append(t->id());
    for (const QString &name : std::as_const(existingNames))
        tools->removeTool(name);

    // Built-in example tools (DateTime, Calculator, SystemInfo).
    tools->addTool(new Example::DateTimeTool);
    tools->addTool(new Example::CalculatorTool);
    tools->addTool(new Example::SystemInfoTool);

    // Tools from every MCP server that has finished initializing. Fresh
    // McpRemoteTool instances are created on each call; ownership transfers
    // to the ToolsManager, which is scoped to m_client and torn down on the
    // next provider switch.
    for (const McpEntry &entry : m_mcpServers) {
        if (!entry.ready || !entry.client)
            continue;
        for (const ToolInfo &info : entry.tools)
            tools->addTool(new McpRemoteTool(entry.client, info));
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

QStringList ChatController::mcpServerNames() const
{
    QStringList names;
    for (const McpEntry &e : m_mcpServers) {
        if (e.ready)
            names.append(QString("%1 (%2 tools)").arg(e.name).arg(e.tools.size()));
        else if (!e.lastError.isEmpty())
            names.append(QString("%1: %2").arg(e.name, e.lastError));
        else
            names.append(QString("%1: connecting…").arg(e.name));
    }
    return names;
}

QString ChatController::resolveConfigPath() const
{
    // Allow override via env var for dev workflows.
    const QByteArray envPath = qgetenv("LLMCORE_MCP_CONFIG");
    if (!envPath.isEmpty())
        return QString::fromLocal8Bit(envPath);

    // Otherwise look for mcp-servers.json next to the executable.
    const QString candidate
        = QDir(QCoreApplication::applicationDirPath()).filePath("mcp-servers.json");
    if (QFile::exists(candidate))
        return candidate;

    // And finally in the current working directory.
    if (QFile::exists("mcp-servers.json"))
        return QFileInfo("mcp-servers.json").absoluteFilePath();

    return {};
}

void ChatController::loadMcpConfig()
{
    const QString path = resolveConfigPath();
    if (path.isEmpty())
        return;

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

    const QJsonObject servers = doc.object().value("mcpServers").toObject();
    if (servers.isEmpty())
        return;

    for (auto it = servers.constBegin(); it != servers.constEnd(); ++it) {
        McpEntry entry;
        entry.name = it.key();
        m_mcpServers.append(std::move(entry));
        McpEntry &stored = m_mcpServers.last();
        initMcpServer(stored, it.value().toObject());
    }
    emit mcpServerNamesChanged();
}

void ChatController::initMcpServer(McpEntry &entry, const QJsonObject &spec)
{
    const bool isHttp = spec.contains("url");
    const bool isStdio = spec.contains("command");

    if (isHttp && isStdio) {
        entry.lastError = QStringLiteral("config has both 'url' and 'command'");
        qWarning().noquote() << "MCP server" << entry.name << ":" << entry.lastError;
        return;
    }

    if (isStdio) {
        StdioLaunchConfig cfg;
        cfg.program = spec.value("command").toString();
        const QJsonArray argsArr = spec.value("args").toArray();
        for (const QJsonValue &v : argsArr)
            cfg.arguments << v.toString();

        // Relative program paths are resolved next to the running executable
        // so the shipped example config "just works".
        QFileInfo progInfo(cfg.program);
        if (progInfo.isRelative()) {
            const QString resolved
                = QDir(QCoreApplication::applicationDirPath()).filePath(cfg.program);
            if (QFile::exists(resolved))
                cfg.program = resolved;
        }

        const QJsonObject envObj = spec.value("env").toObject();
        if (!envObj.isEmpty()) {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            for (auto it = envObj.constBegin(); it != envObj.constEnd(); ++it)
                env.insert(it.key(), it.value().toString());
            cfg.environment = env;
        }
        cfg.workingDirectory = spec.value("cwd").toString();

        entry.transport = new McpStdioClientTransport(cfg, this);
    } else if (isHttp) {
        HttpTransportConfig cfg;
        cfg.endpoint = QUrl(spec.value("url").toString());
        const QString specStr = spec.value("spec").toString();
        // Only 2024-11-05 uses the old two-endpoint wire format; every later
        // revision shares the Streamable HTTP format (2025-03-26+).
        if (specStr == QLatin1String("2024-11-05"))
            cfg.spec = McpHttpSpec::V2024_11_05;
        else
            cfg.spec = McpHttpSpec::V2025_03_26;

        const QJsonObject headersObj = spec.value("headers").toObject();
        for (auto it = headersObj.constBegin(); it != headersObj.constEnd(); ++it)
            cfg.headers.insert(it.key(), it.value().toString());

        entry.transport = new McpHttpTransport(cfg, nullptr, this);
    } else {
        entry.lastError = QStringLiteral("config has neither 'url' nor 'command'");
        qWarning().noquote() << "MCP server" << entry.name << ":" << entry.lastError;
        return;
    }

    entry.client = new McpClient(
        entry.transport, Implementation{"llmcore-chat-example", "0.1.0"}, this);

    connect(
        entry.client, &McpClient::errorOccurred, this, [this, name = entry.name](const QString &e) {
            qWarning().noquote() << "MCP server" << name << "error:" << e;
        });

    connectMcpClient(entry);
}

void ChatController::connectMcpClient(McpEntry &entry)
{
    const QString serverName = entry.name;
    auto *client = entry.client;
    if (!client)
        return;

    auto initFuture = client->connectAndInitialize(std::chrono::seconds(10));
    auto *initWatcher = new QFutureWatcher<InitializeResult>(this);
    connect(
        initWatcher,
        &QFutureWatcher<InitializeResult>::finished,
        this,
        [this, initWatcher, serverName, client]() {
            try {
                (void) initWatcher->result();
            } catch (const std::exception &e) {
                for (McpEntry &e2 : m_mcpServers) {
                    if (e2.name == serverName) {
                        e2.lastError = QString::fromUtf8(e.what());
                        break;
                    }
                }
                emit mcpServerNamesChanged();
                qWarning().noquote()
                    << "MCP server" << serverName << "init failed:" << e.what();
                initWatcher->deleteLater();
                return;
            }
            initWatcher->deleteLater();

            auto listFuture = client->listTools();
            auto *listWatcher = new QFutureWatcher<QList<ToolInfo>>(this);
            connect(
                listWatcher,
                &QFutureWatcher<QList<ToolInfo>>::finished,
                this,
                [this, listWatcher, serverName]() {
                    try {
                        const QList<ToolInfo> tools = listWatcher->result();
                        for (McpEntry &e : m_mcpServers) {
                            if (e.name != serverName)
                                continue;
                            e.tools = tools;
                            e.ready = true;
                            e.lastError.clear();
                            break;
                        }
                        emit mcpServerNamesChanged();
                        // If the user already picked a provider, re-register
                        // tools on its ToolsManager so the new MCP tools show
                        // up without a reconnect.
                        if (m_client)
                            registerTools();
                    } catch (const std::exception &e) {
                        for (McpEntry &entry : m_mcpServers) {
                            if (entry.name == serverName) {
                                entry.lastError
                                    = QString("tools/list failed: %1").arg(e.what());
                                break;
                            }
                        }
                        emit mcpServerNamesChanged();
                    }
                    listWatcher->deleteLater();
                });
            listWatcher->setFuture(listFuture);
        });
    initWatcher->setFuture(initFuture);
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
