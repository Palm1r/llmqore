// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>

#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <LLMQore/AcpFileSystemProvider.hpp>
#include <LLMQore/AcpPermissionProvider.hpp>
#include <LLMQore/AcpTerminalProvider.hpp>
#include <LLMQore/AcpTypes.hpp>
#include <LLMQore/LLMQore_global.h>
#include <LLMQore/RpcTransport.hpp>

namespace LLMQore::Rpc {
class JsonRpcSession;
}

namespace LLMQore::Acp {

class LLMQORE_EXPORT AcpClient : public QObject
{
    Q_OBJECT
public:
    explicit AcpClient(
        Rpc::Transport *transport,
        Implementation clientInfo = {QStringLiteral("LLMQore"), QStringLiteral("0.6.0"),
                                     QStringLiteral("LLMQore")},
        QObject *parent = nullptr);
    ~AcpClient() override;

    // --- Host capabilities (set before connectAndInitialize) ---
    // Presence of a provider flips the matching clientCapabilities flag.
    void setPermissionProvider(AcpPermissionProvider *provider);
    void setFileSystemProvider(AcpFileSystemProvider *provider);
    void setTerminalProvider(AcpTerminalProvider *provider);

    AcpPermissionProvider *permissionProvider() const { return m_permissionProvider.data(); }
    AcpFileSystemProvider *fileSystemProvider() const { return m_fsProvider.data(); }
    AcpTerminalProvider *terminalProvider() const { return m_terminalProvider.data(); }

    // --- Outgoing (client -> agent) ---
    QFuture<InitializeResult> connectAndInitialize(
        std::chrono::milliseconds timeout = std::chrono::seconds(15));
    QFuture<void> authenticate(
        const QString &methodId, std::chrono::milliseconds timeout = std::chrono::seconds(30));
    QFuture<NewSessionResult> newSession(
        const NewSessionParams &params,
        std::chrono::milliseconds timeout = std::chrono::seconds(30));
    QFuture<NewSessionResult> loadSession(
        const LoadSessionParams &params,
        std::chrono::milliseconds timeout = std::chrono::seconds(60));
    QFuture<PromptResult> prompt(
        const QString &sessionId,
        const QList<ContentBlock> &blocks,
        std::chrono::milliseconds timeout = std::chrono::minutes(10));
    void cancel(const QString &sessionId);
    QFuture<void> setMode(
        const QString &sessionId,
        const QString &modeId,
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    // --- State ---
    bool isInitialized() const { return m_initialized; }
    const InitializeResult &agentInfo() const { return m_initResult; }
    ClientCapabilities clientCapabilities() const;
    QStringList sessionIds() const { return m_sessions.keys(); }

    Rpc::JsonRpcSession *session() const { return m_session; }
    Rpc::Transport *transport() const { return m_transport.data(); }

    void shutdown();

signals:
    void initialized(const LLMQore::Acp::InitializeResult &info);

    // Streaming session/update (each carries the originating sessionId).
    void userMessageChunk(const QString &sessionId, const LLMQore::Acp::ContentBlock &content);
    void agentMessageChunk(const QString &sessionId, const LLMQore::Acp::ContentBlock &content);
    void agentThoughtChunk(const QString &sessionId, const LLMQore::Acp::ContentBlock &content);
    void toolCallStarted(const QString &sessionId, const LLMQore::Acp::ToolCall &toolCall);
    void toolCallUpdated(const QString &sessionId, const LLMQore::Acp::ToolCall &toolCall);
    void planUpdated(const QString &sessionId, const LLMQore::Acp::Plan &plan);
    void availableCommandsUpdated(
        const QString &sessionId, const QList<LLMQore::Acp::AvailableCommand> &commands);
    void modeChanged(const QString &sessionId, const QString &modeId);
    // Token-usage report (usage_update); payload shape is agent-defined, passed raw.
    void usageUpdated(const QString &sessionId, const QJsonObject &usage);

    void promptFinished(const QString &sessionId, const QString &stopReason);

    void agentStderr(const QString &line);
    void errorOccurred(const QString &error);
    void disconnected();

private:
    void installHandlers();
    void handleSessionUpdate(const QJsonObject &params);

    struct SessionState
    {
        QHash<QString, ToolCall> tools;
    };

    QPointer<Rpc::Transport> m_transport;
    Rpc::JsonRpcSession *m_session = nullptr;
    Implementation m_clientInfo;
    InitializeResult m_initResult;
    bool m_initialized = false;

    QHash<QString, SessionState> m_sessions;

    QPointer<AcpPermissionProvider> m_permissionProvider;
    QPointer<AcpFileSystemProvider> m_fsProvider;
    QPointer<AcpTerminalProvider> m_terminalProvider;
};

} // namespace LLMQore::Acp
