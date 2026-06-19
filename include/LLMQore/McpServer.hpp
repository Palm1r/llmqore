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

#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpTypes.hpp>
#include <LLMQore/RpcTransport.hpp>
#include <LLMQore/Version.hpp>

namespace LLMQore {
class BaseTool;
class ToolRegistry;
} // namespace LLMQore

namespace LLMQore::Mcp {

class BaseResourceProvider;
class BasePromptProvider;

struct LLMQORE_EXPORT McpServerConfig
{
    Implementation serverInfo{"LLMQore-server", QStringLiteral(LLMQORE_VERSION_STRING)};
    QString instructions;
    bool advertiseLogging = true;
};

class LLMQORE_EXPORT McpServer : public QObject
{
    Q_OBJECT
public:
    McpServer(
        Rpc::Transport *transport, McpServerConfig config, QObject *parent = nullptr);
    ~McpServer() override;

    void setToolRegistry(LLMQore::ToolRegistry *registry);
    void addTool(LLMQore::BaseTool *tool);
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

    Rpc::Transport *transport() const { return m_transport; }
    Rpc::JsonRpcSession *session() const { return m_session; }

signals:
    void clientInitialized(const LLMQore::Mcp::Implementation &clientInfo);
    void toolCallReceived(const QString &toolName);
    void resourceRead(const QString &uri);
    void promptRequested(const QString &name);
    void logLevelChanged(const QString &level);
    void errorOccurred(const QString &error);

private:
    void installHandlers();
    QList<LLMQore::BaseTool *> collectTools() const;
    LLMQore::BaseTool *findTool(const QString &name) const;

    QPointer<Rpc::Transport> m_transport;
    Rpc::JsonRpcSession *m_session = nullptr;
    McpServerConfig m_config;

    QPointer<LLMQore::ToolRegistry> m_toolRegistry;
    // QMap for stable alphabetical ordering in tools/list.
    QMap<QString, QPointer<LLMQore::BaseTool>> m_standaloneTools;
    QList<QPointer<BaseResourceProvider>> m_resourceProviders;
    QList<QPointer<BasePromptProvider>> m_promptProviders;

    QString m_logLevel = QStringLiteral("info");
    bool m_initialized = false;
    ClientCapabilities m_clientCapabilities;
};

} // namespace LLMQore::Mcp
