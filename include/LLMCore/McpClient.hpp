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

#include <LLMCore/BaseClient.hpp>
#include <LLMCore/BaseElicitationProvider.hpp>
#include <LLMCore/BaseRootsProvider.hpp>
#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTransport.hpp>
#include <LLMCore/McpTypes.hpp>
#include <LLMCore/ToolResult.hpp>

namespace LLMCore::Mcp {

class McpSession;

class LLMCORE_EXPORT McpClient : public QObject
{
    Q_OBJECT
public:
    explicit McpClient(
        McpTransport *transport,
        Implementation clientInfo = {"LLMCore", "0.1.0"},
        QObject *parent = nullptr);
    ~McpClient() override;

    QFuture<InitializeResult> connectAndInitialize(
        std::chrono::milliseconds timeout = std::chrono::seconds(15));

    QFuture<void> ping(std::chrono::milliseconds timeout = std::chrono::seconds(5));
    QFuture<void> setLogLevel(const QString &level);

    QFuture<QList<ToolInfo>> listTools();
    QFuture<LLMCore::ToolResult> callTool(const QString &name, const QJsonObject &arguments);

    using ProgressCallback
        = std::function<void(double progress, double total, const QString &message)>;

    struct LLMCORE_EXPORT CancellableToolCall
    {
        QFuture<LLMCore::ToolResult> future;
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
        LLMCore::BaseClient *client, SamplingPayloadBuilder builder);
    LLMCore::BaseClient *samplingClient() const { return m_samplingClient.data(); }

    void setElicitationProvider(BaseElicitationProvider *provider);
    BaseElicitationProvider *elicitationProvider() const
    {
        return m_elicitationProvider.data();
    }

    bool isInitialized() const { return m_initialized; }
    const InitializeResult &serverInfo() const { return m_initResult; }
    const QList<ToolInfo> &cachedTools() const { return m_cachedTools; }

    McpTransport *transport() const { return m_transport; }
    McpSession *session() const { return m_session; }

    void shutdown();

signals:
    void initialized(const LLMCore::Mcp::InitializeResult &info);
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

    QPointer<McpTransport> m_transport;
    McpSession *m_session = nullptr;
    Implementation m_clientInfo;
    InitializeResult m_initResult;
    QList<ToolInfo> m_cachedTools;
    bool m_initialized = false;
    QPointer<BaseRootsProvider> m_rootsProvider;
    QPointer<LLMCore::BaseClient> m_samplingClient;
    SamplingPayloadBuilder m_samplingBuilder;
    QPointer<BaseElicitationProvider> m_elicitationProvider;
};

} // namespace LLMCore::Mcp
