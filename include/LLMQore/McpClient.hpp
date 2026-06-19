// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <functional>

#include <QFuture>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/BaseElicitationProvider.hpp>
#include <LLMQore/BaseRootsProvider.hpp>
#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/LLMQore_global.h>
#include <LLMQore/McpTypes.hpp>
#include <LLMQore/RpcTransport.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT McpClient : public QObject
{
    Q_OBJECT
public:
    explicit McpClient(
        Rpc::Transport *transport,
        Implementation clientInfo = {"LLMQore", "0.1.0"},
        QObject *parent = nullptr);
    ~McpClient() override;

    QFuture<InitializeResult> connectAndInitialize(
        std::chrono::milliseconds timeout = std::chrono::seconds(15));

    QFuture<void> ping(std::chrono::milliseconds timeout = std::chrono::seconds(5));
    QFuture<void> setLogLevel(const QString &level);

    QFuture<QList<ToolInfo>> listTools();
    QFuture<LLMQore::ToolResult> callTool(const QString &name, const QJsonObject &arguments);

    using ProgressCallback
        = std::function<void(double progress, double total, const QString &message)>;

    struct LLMQORE_EXPORT CancellableToolCall
    {
        QFuture<LLMQore::ToolResult> future;
        QString requestId;
        QString progressToken;
    };

    CancellableToolCall callToolWithProgress(
        const QString &name,
        const QJsonObject &arguments,
        ProgressCallback onProgress = {});

    void cancel(const QString &requestId, const QString &reason = {});

    QFuture<QList<ResourceInfo>> listResources();
    QFuture<QList<ResourceTemplate>> listResourceTemplates();
    QFuture<ResourceContents> readResource(const QString &uri);
    QFuture<void> subscribeResource(const QString &uri);
    QFuture<void> unsubscribeResource(const QString &uri);

    QFuture<QList<PromptInfo>> listPrompts();
    QFuture<PromptGetResult> getPrompt(
        const QString &name, const QJsonObject &arguments = {});

    QFuture<CompletionResult> complete(
        const CompletionReference &ref,
        const QString &argumentName,
        const QString &partialValue,
        const QJsonObject &contextArguments = {});

    void setRootsProvider(BaseRootsProvider *provider);
    BaseRootsProvider *rootsProvider() const { return m_rootsProvider.data(); }

    using SamplingPayloadBuilder
        = std::function<QJsonObject(const CreateMessageParams &)>;

    void setSamplingClient(
        LLMQore::BaseClient *client, SamplingPayloadBuilder builder);
    LLMQore::BaseClient *samplingClient() const { return m_samplingClient.data(); }

    void setElicitationProvider(BaseElicitationProvider *provider);
    BaseElicitationProvider *elicitationProvider() const
    {
        return m_elicitationProvider.data();
    }

    bool isInitialized() const { return m_initialized; }
    const InitializeResult &serverInfo() const { return m_initResult; }
    const QList<ToolInfo> &cachedTools() const { return m_cachedTools; }

    Rpc::Transport *transport() const { return m_transport; }
    Rpc::JsonRpcSession *session() const { return m_session; }

    void shutdown();

signals:
    void initialized(const LLMQore::Mcp::InitializeResult &info);
    void toolsChanged();
    void resourcesChanged();
    void resourceUpdated(const QString &uri);
    void promptsChanged();
    void logMessage(
        const QString &level,
        const QString &logger,
        const QJsonValue &data,
        const QString &message);
    void errorOccurred(const QString &error);
    void disconnected();

private:
    void installHandlers();

    QFuture<QJsonValue> sendInitialized(
        const QString &method, const QJsonObject &params = {});

    QPointer<Rpc::Transport> m_transport;
    Rpc::JsonRpcSession *m_session = nullptr;
    Implementation m_clientInfo;
    InitializeResult m_initResult;
    QList<ToolInfo> m_cachedTools;
    bool m_initialized = false;
    QPointer<BaseRootsProvider> m_rootsProvider;
    QPointer<LLMQore::BaseClient> m_samplingClient;
    SamplingPayloadBuilder m_samplingBuilder;
    QPointer<BaseElicitationProvider> m_elicitationProvider;
};

} // namespace LLMQore::Mcp
