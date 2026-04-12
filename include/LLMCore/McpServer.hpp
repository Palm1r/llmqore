// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>

#include <QFuture>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTypes.hpp>

namespace LLMCore {
class BaseTool;
class ToolsManager;
} // namespace LLMCore

namespace LLMCore::Mcp {

class McpTransport;
class McpSession;
class BaseResourceProvider;
class BasePromptProvider;

struct LLMCORE_EXPORT McpServerConfig
{
    Implementation serverInfo{"LLMCore-server", "0.1.0"};
    QString instructions;
    bool advertiseLogging = true;
};

class LLMCORE_EXPORT McpServer : public QObject
{
    Q_OBJECT
public:
    McpServer(
        McpTransport *transport, McpServerConfig config, QObject *parent = nullptr);
    ~McpServer() override;

    void setToolsManager(LLMCore::ToolsManager *manager);
    void addTool(LLMCore::BaseTool *tool);
    void removeTool(const QString &name);

    void addResourceProvider(BaseResourceProvider *provider);
    void removeResourceProvider(BaseResourceProvider *provider);

    void addPromptProvider(BasePromptProvider *provider);
    void removePromptProvider(BasePromptProvider *provider);

    void sendLogMessage(
        const QString &level,
        const QString &logger,
        const QJsonValue &data,
        const QString &message = {});

    QFuture<CreateMessageResult> createSamplingMessage(
        const CreateMessageParams &params,
        std::chrono::milliseconds timeout = std::chrono::seconds(120));

    QFuture<ElicitResult> createElicitation(
        const ElicitRequestParams &params,
        std::chrono::milliseconds timeout = std::chrono::seconds(300));

    QString currentLogLevel() const { return m_logLevel; }

    void start();
    void stop();

    McpTransport *transport() const { return m_transport; }
    McpSession *session() const { return m_session; }

signals:
    void clientInitialized(const LLMCore::Mcp::Implementation &clientInfo);
    void toolCallReceived(const QString &toolName);
    void resourceRead(const QString &uri);
    void promptRequested(const QString &name);
    void logLevelChanged(const QString &level);
    void errorOccurred(const QString &error);

private:
    void installHandlers();
    QList<LLMCore::BaseTool *> collectTools() const;
    LLMCore::BaseTool *findTool(const QString &name) const;

    QPointer<McpTransport> m_transport;
    McpSession *m_session = nullptr;
    McpServerConfig m_config;

    QPointer<LLMCore::ToolsManager> m_toolsManager;
    // QMap for stable alphabetical ordering in tools/list.
    QMap<QString, QPointer<LLMCore::BaseTool>> m_standaloneTools;
    QList<QPointer<BaseResourceProvider>> m_resourceProviders;
    QList<QPointer<BasePromptProvider>> m_promptProviders;

    QString m_logLevel = QStringLiteral("info");
    bool m_initialized = false;
    ClientCapabilities m_clientCapabilities;
};

} // namespace LLMCore::Mcp
