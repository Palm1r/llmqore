// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/AcpClient.hpp>

#include <memory>

#include <QJsonArray>
#include <QJsonObject>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/ProtocolPeer.hpp>
#include <LLMQore/RpcExceptions.hpp>
#include <LLMQore/RpcStdioTransport.hpp>

#include "core/ThreadAffinity.hpp"

namespace LLMQore::Acp {

namespace {

ToolCall mergeToolCall(ToolCall base, const ToolCall &upd)
{
    if (!upd.toolCallId.isEmpty())
        base.toolCallId = upd.toolCallId;
    if (!upd.title.isEmpty())
        base.title = upd.title;
    if (!upd.kind.isEmpty())
        base.kind = upd.kind;
    if (!upd.status.isEmpty())
        base.status = upd.status;
    if (!upd.content.isEmpty())
        base.content = upd.content;
    if (!upd.locations.isEmpty())
        base.locations = upd.locations;
    if (!upd.rawInput.isEmpty())
        base.rawInput = upd.rawInput;
    if (!upd.rawOutput.isEmpty())
        base.rawOutput = upd.rawOutput;
    return base;
}

} // namespace

AcpClient::AcpClient(Rpc::Transport *transport, Implementation clientInfo, QObject *parent)
    : QObject(parent)
    , m_peer(new Rpc::ProtocolPeer(transport, this))
    , m_clientInfo(std::move(clientInfo))
{
    registerMetatypes();

    connect(m_peer, &Rpc::ProtocolPeer::closed, this, &AcpClient::disconnected);
    connect(m_peer, &Rpc::ProtocolPeer::errorOccurred, this, &AcpClient::errorOccurred);
    if (auto *stdio = qobject_cast<Rpc::StdioClientTransport *>(m_peer->transport()))
        connect(stdio, &Rpc::StdioClientTransport::stderrLine, this, &AcpClient::agentStderr);

    installHandlers();
}

AcpClient::~AcpClient() = default;

ClientCapabilities AcpClient::clientCapabilities() const
{
    ClientCapabilities caps;
    caps.fs.readTextFile = !m_fsProvider.isNull();
    caps.fs.writeTextFile = !m_fsProvider.isNull() && m_fsProvider->supportsWrite();
    caps.terminal = !m_terminalProvider.isNull();
    if (!m_booleanConfigOptions)
        caps.session.configOptions.boolean.reset();
    return caps;
}

void AcpClient::setPermissionProvider(AcpPermissionProvider *provider)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_permissionProvider = provider;
}

void AcpClient::setFileSystemProvider(AcpFileSystemProvider *provider)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_fsProvider = provider;
}

void AcpClient::setTerminalProvider(AcpTerminalProvider *provider)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_terminalProvider = provider;
}

void AcpClient::setBooleanConfigOptionsSupported(bool supported)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_booleanConfigOptions = supported;
}

QFuture<InitializeResult> AcpClient::connectAndInitialize(std::chrono::milliseconds timeout)
{
    InitializeParams params;
    params.protocolVersion = kAcpProtocolVersion;
    params.clientCapabilities = clientCapabilities();
    params.clientInfo = m_clientInfo;

    return LLMQore::compat(
               m_peer->handshake(QLatin1String(Method::Initialize), params.toJson(), timeout))
        .then(this, [this](const QJsonValue &v) -> InitializeResult {
            InitializeResult r = InitializeResult::fromJson(v.toObject());
            m_peer->warnOnUnknownVersion(
                QString::number(r.protocolVersion), knownProtocolVersions(), llmAcpLog());
            m_initResult = r;
            emit initialized(r);
            return r;
        });
}

QFuture<void> AcpClient::authenticate(const QString &methodId, std::chrono::milliseconds timeout)
{
    return LLMQore::compat(m_peer->requestUngated(
                               QLatin1String(Method::Authenticate),
                               QJsonObject{{"methodId", methodId}},
                               timeout))
        .then(this, [](const QJsonValue &) {});
}

QFuture<NewSessionResult> AcpClient::newSession(
    const NewSessionParams &params, std::chrono::milliseconds timeout)
{
    return LLMQore::compat(
               m_peer->request(QLatin1String(Method::NewSession), params.toJson(), timeout))
        .then(this, [this](const QJsonValue &v) -> NewSessionResult {
            NewSessionResult r = NewSessionResult::fromJson(v.toObject());
            if (!r.sessionId.isEmpty() && !m_sessions.contains(r.sessionId))
                m_sessions.insert(r.sessionId, SessionState{});
            return r;
        });
}

QFuture<NewSessionResult> AcpClient::loadSession(
    const LoadSessionParams &params, std::chrono::milliseconds timeout)
{
    if (!params.sessionId.isEmpty() && !m_sessions.contains(params.sessionId))
        m_sessions.insert(params.sessionId, SessionState{});
    return LLMQore::compat(
               m_peer->request(QLatin1String(Method::LoadSession), params.toJson(), timeout))
        .then(this, [](const QJsonValue &v) -> NewSessionResult {
            return NewSessionResult::fromJson(v.toObject());
        });
}

QFuture<PromptResult> AcpClient::prompt(
    const QString &sessionId,
    const QList<ContentBlock> &blocks,
    std::chrono::milliseconds timeout)
{
    PromptParams params;
    params.sessionId = sessionId;
    params.prompt = blocks;

    return LLMQore::compat(
               m_peer->request(QLatin1String(Method::Prompt), params.toJson(), timeout))
        .then(this, [this, sessionId](const QJsonValue &v) -> PromptResult {
            PromptResult r = PromptResult::fromJson(v.toObject());
            auto it = m_sessions.find(sessionId);
            if (it != m_sessions.end())
                it->tools.clear();
            emit promptFinished(sessionId, r.stopReason);
            return r;
        });
}

void AcpClient::cancel(const QString &sessionId)
{
    m_peer->notify(QLatin1String(Method::Cancel), QJsonObject{{"sessionId", sessionId}});
}

QFuture<void> AcpClient::setMode(
    const QString &sessionId, const QString &modeId, std::chrono::milliseconds timeout)
{
    return LLMQore::compat(m_peer->request(
                               QLatin1String(Method::SetMode),
                               QJsonObject{{"sessionId", sessionId}, {"modeId", modeId}},
                               timeout))
        .then(this, [](const QJsonValue &) {});
}

QFuture<QList<SessionConfigOption>> AcpClient::setConfigOption(
    const QString &sessionId,
    const QString &configId,
    bool enabled,
    std::chrono::milliseconds timeout)
{
    QJsonObject params{
        {"sessionId", sessionId},
        {"configId", configId},
        {"value", enabled},
        {"type", QStringLiteral("boolean")}};
    return LLMQore::compat(m_peer->request(QLatin1String(Method::SetConfigOption), params, timeout))
        .then(this, [](const QJsonValue &v) {
            return configOptionsFromJson(v.toObject().value("configOptions").toArray());
        });
}

QFuture<QList<SessionConfigOption>> AcpClient::setConfigOption(
    const QString &sessionId,
    const QString &configId,
    const QString &valueId,
    std::chrono::milliseconds timeout)
{
    QJsonObject params{{"sessionId", sessionId}, {"configId", configId}, {"value", valueId}};
    return LLMQore::compat(m_peer->request(QLatin1String(Method::SetConfigOption), params, timeout))
        .then(this, [](const QJsonValue &v) {
            return configOptionsFromJson(v.toObject().value("configOptions").toArray());
        });
}

void AcpClient::shutdown()
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_peer->session()->abortPending(QStringLiteral("AcpClient shutdown"));
    m_peer->close();
}

void AcpClient::handleSessionUpdate(const QJsonObject &params)
{
    const SessionNotification n = SessionNotification::fromJson(params);
    const QString sid = n.sessionId;
    const SessionUpdate &u = n.update;
    const QString &kind = u.sessionUpdate;

    if (kind == QLatin1String(SessionUpdateKind::UserMessageChunk)) {
        if (u.content)
            emit userMessageChunk(sid, *u.content);
    } else if (kind == QLatin1String(SessionUpdateKind::AgentMessageChunk)) {
        if (u.content)
            emit agentMessageChunk(sid, *u.content);
    } else if (kind == QLatin1String(SessionUpdateKind::AgentThoughtChunk)) {
        if (u.content)
            emit agentThoughtChunk(sid, *u.content);
    } else if (kind == QLatin1String(SessionUpdateKind::ToolCall)) {
        if (u.toolCall) {
            m_sessions[sid].tools.insert(u.toolCall->toolCallId, *u.toolCall);
            emit toolCallStarted(sid, *u.toolCall);
        }
    } else if (kind == QLatin1String(SessionUpdateKind::ToolCallUpdate)) {
        if (u.toolCall) {
            SessionState &st = m_sessions[sid];
            const ToolCall merged
                = mergeToolCall(st.tools.value(u.toolCall->toolCallId), *u.toolCall);
            st.tools.insert(merged.toolCallId, merged);
            emit toolCallUpdated(sid, merged);
        }
    } else if (kind == QLatin1String(SessionUpdateKind::Plan)) {
        if (u.plan)
            emit planUpdated(sid, *u.plan);
    } else if (kind == QLatin1String(SessionUpdateKind::AvailableCommandsUpdate)) {
        emit availableCommandsUpdated(sid, u.availableCommands);
    } else if (kind == QLatin1String(SessionUpdateKind::CurrentModeUpdate)) {
        emit modeChanged(sid, u.currentModeId);
    } else if (kind == QLatin1String(SessionUpdateKind::ConfigOptionUpdate)) {
        emit configOptionsUpdated(sid, u.configOptions);
    } else if (kind == QLatin1String(SessionUpdateKind::UsageUpdate)) {
        emit usageUpdated(sid, u.usage);
    } else if (kind == QLatin1String(SessionUpdateKind::SessionInfoUpdate)) {
        emit sessionInfoUpdated(sid, u.title);
    } else {
        qCDebug(llmAcpLog).noquote() << "ACP: unknown session/update kind:" << kind;
    }
}

void AcpClient::installHandlers()
{
    m_peer->bindNotification(
        QLatin1String(Method::SessionUpdate),
        [this](const QJsonObject &params) { handleSessionUpdate(params); });

    m_peer->bindRequest(
        QLatin1String(Method::RequestPermission),
        QStringLiteral("session/request_permission"),
        []() { return true; },
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const RequestPermissionParams p = RequestPermissionParams::fromJson(params);
            if (m_permissionProvider.isNull()) {
                return LLMQore::readyFuture<QJsonValue>(
                    RequestPermissionResult::cancelled().toJson());
            }
            return LLMQore::compat(
                       m_permissionProvider->requestPermission(p.sessionId, p.toolCall, p.options))
                .then(this, [](const RequestPermissionResult &r) { return QJsonValue(r.toJson()); });
        });

    m_peer->bindRequest(
        QLatin1String(Method::FsReadTextFile),
        QStringLiteral("fs/read_text_file"),
        [this]() { return !m_fsProvider.isNull(); },
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const ReadTextFileParams p = ReadTextFileParams::fromJson(params);
            return LLMQore::compat(
                       m_fsProvider->readTextFile(p.sessionId, p.path, p.line, p.limit))
                .then(this, [](const QString &content) {
                    ReadTextFileResult r;
                    r.content = content;
                    return QJsonValue(r.toJson());
                });
        });

    m_peer->bindRequest(
        QLatin1String(Method::FsWriteTextFile),
        QStringLiteral("fs/write_text_file"),
        [this]() { return !m_fsProvider.isNull() && m_fsProvider->supportsWrite(); },
        [this](const QJsonObject &params) {
            const WriteTextFileParams p = WriteTextFileParams::fromJson(params);
            return m_fsProvider->writeTextFile(p.sessionId, p.path, p.content);
        });

    const auto hasTerminal = [this]() { return !m_terminalProvider.isNull(); };
    const QString terminal = QStringLiteral("terminal");

    m_peer->bindRequest(
        QLatin1String(Method::TerminalCreate), terminal, hasTerminal,
        [this](const QJsonObject &params) {
            return m_terminalProvider->createTerminal(CreateTerminalParams::fromJson(params));
        });

    m_peer->bindRequest(
        QLatin1String(Method::TerminalOutput), terminal, hasTerminal,
        [this](const QJsonObject &params) {
            const TerminalOutputParams p = TerminalOutputParams::fromJson(params);
            return m_terminalProvider->terminalOutput(p.sessionId, p.terminalId);
        });

    m_peer->bindRequest(
        QLatin1String(Method::TerminalWaitForExit), terminal, hasTerminal,
        [this](const QJsonObject &params) {
            const TerminalRefParams p = TerminalRefParams::fromJson(params);
            return m_terminalProvider->waitForExit(p.sessionId, p.terminalId);
        });

    m_peer->bindRequest(
        QLatin1String(Method::TerminalKill), terminal, hasTerminal,
        [this](const QJsonObject &params) {
            const TerminalRefParams p = TerminalRefParams::fromJson(params);
            return m_terminalProvider->killTerminal(p.sessionId, p.terminalId);
        });

    m_peer->bindRequest(
        QLatin1String(Method::TerminalRelease), terminal, hasTerminal,
        [this](const QJsonObject &params) {
            const TerminalRefParams p = TerminalRefParams::fromJson(params);
            return m_terminalProvider->releaseTerminal(p.sessionId, p.terminalId);
        });
}

} // namespace LLMQore::Acp
