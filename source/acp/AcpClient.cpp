// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/AcpClient.hpp>

#include <memory>

#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/RpcExceptions.hpp>

namespace LLMQore::Acp {

namespace {

void registerMetatypesOnce()
{
    static const bool once = []() {
        qRegisterMetaType<LLMQore::Acp::ContentBlock>();
        qRegisterMetaType<LLMQore::Acp::ToolCall>();
        qRegisterMetaType<LLMQore::Acp::Plan>();
        qRegisterMetaType<LLMQore::Acp::InitializeResult>();
        qRegisterMetaType<LLMQore::Acp::NewSessionResult>();
        qRegisterMetaType<LLMQore::Acp::PromptResult>();
        qRegisterMetaType<QList<LLMQore::Acp::AvailableCommand>>();
        return true;
    }();
    Q_UNUSED(once);
}

QFuture<QJsonValue> failedJson(int code, const QString &message)
{
    auto promise = std::make_shared<QPromise<QJsonValue>>();
    promise->start();
    promise->setException(std::make_exception_ptr(Rpc::RemoteError(code, message)));
    promise->finish();
    return promise->future();
}

QFuture<QJsonValue> resolvedJson(const QJsonValue &value)
{
    auto promise = std::make_shared<QPromise<QJsonValue>>();
    promise->start();
    promise->addResult(value);
    promise->finish();
    return promise->future();
}

// tool_call_update carries the current state of a tool call; merge the
// non-empty fields onto the call the UI already shows.
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
    , m_transport(transport)
    , m_session(new Rpc::JsonRpcSession(transport, this))
    , m_clientInfo(std::move(clientInfo))
{
    registerMetatypesOnce();

    if (m_transport) {
        connect(m_transport, &Rpc::Transport::stderrLine, this, &AcpClient::agentStderr);
        connect(m_transport, &Rpc::Transport::closed, this, &AcpClient::disconnected);
        connect(m_transport, &Rpc::Transport::errorOccurred, this, &AcpClient::errorOccurred);
    }

    installHandlers();
}

AcpClient::~AcpClient() = default;

ClientCapabilities AcpClient::clientCapabilities() const
{
    ClientCapabilities caps;
    caps.fs.readTextFile = !m_fsProvider.isNull();
    caps.fs.writeTextFile = !m_fsProvider.isNull() && m_fsProvider->supportsWrite();
    caps.terminal = !m_terminalProvider.isNull();
    return caps;
}

void AcpClient::setPermissionProvider(AcpPermissionProvider *provider)
{
    m_permissionProvider = provider;
}

void AcpClient::setFileSystemProvider(AcpFileSystemProvider *provider)
{
    m_fsProvider = provider;
}

void AcpClient::setTerminalProvider(AcpTerminalProvider *provider)
{
    m_terminalProvider = provider;
}

QFuture<InitializeResult> AcpClient::connectAndInitialize(std::chrono::milliseconds timeout)
{
    if (m_transport && !m_transport->isOpen())
        m_transport->start();

    InitializeParams params;
    params.protocolVersion = kAcpProtocolVersion;
    params.clientCapabilities = clientCapabilities();
    params.clientInfo = m_clientInfo;

    QFuture<QJsonValue> raw
        = m_session->sendRequest(QLatin1String(Method::Initialize), params.toJson(), timeout);
    return LLMQore::futureThen(this, raw, [this](const QJsonValue &v) -> InitializeResult {
        InitializeResult r = InitializeResult::fromJson(v.toObject());
        m_initResult = r;
        m_initialized = true;
        emit initialized(r);
        return r;
    });
}

QFuture<void> AcpClient::authenticate(const QString &methodId, std::chrono::milliseconds timeout)
{
    QFuture<QJsonValue> raw = m_session->sendRequest(
        QLatin1String(Method::Authenticate), QJsonObject{{"methodId", methodId}}, timeout);
    return LLMQore::futureThen(this, raw, [](const QJsonValue &) {});
}

QFuture<NewSessionResult> AcpClient::newSession(
    const NewSessionParams &params, std::chrono::milliseconds timeout)
{
    QFuture<QJsonValue> raw
        = m_session->sendRequest(QLatin1String(Method::NewSession), params.toJson(), timeout);
    return LLMQore::futureThen(this, raw, [this](const QJsonValue &v) -> NewSessionResult {
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
    QFuture<QJsonValue> raw
        = m_session->sendRequest(QLatin1String(Method::LoadSession), params.toJson(), timeout);
    return LLMQore::futureThen(this, raw, [](const QJsonValue &v) -> NewSessionResult {
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

    QFuture<QJsonValue> raw
        = m_session->sendRequest(QLatin1String(Method::Prompt), params.toJson(), timeout);
    return LLMQore::futureThen(
        this, raw, [this, sessionId](const QJsonValue &v) -> PromptResult {
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
    m_session->sendNotification(
        QLatin1String(Method::Cancel), QJsonObject{{"sessionId", sessionId}});
}

QFuture<void> AcpClient::setMode(
    const QString &sessionId, const QString &modeId, std::chrono::milliseconds timeout)
{
    QFuture<QJsonValue> raw = m_session->sendRequest(
        QLatin1String(Method::SetMode),
        QJsonObject{{"sessionId", sessionId}, {"modeId", modeId}},
        timeout);
    return LLMQore::futureThen(this, raw, [](const QJsonValue &) {});
}

void AcpClient::shutdown()
{
    if (m_session)
        m_session->abortPending(QStringLiteral("AcpClient shutdown"));
    if (m_transport)
        m_transport->stop();
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
    } else if (kind == QLatin1String(SessionUpdateKind::UsageUpdate)) {
        emit usageUpdated(sid, params.value("update").toObject());
    } else {
        qCDebug(llmAcpLog).noquote() << "ACP: unknown session/update kind:" << kind;
    }
}

void AcpClient::installHandlers()
{
    m_session->setNotificationHandler(
        QLatin1String(Method::SessionUpdate),
        [this](const QJsonObject &params) { handleSessionUpdate(params); });

    // session/request_permission — always handled; no provider ⇒ cancel.
    m_session->setRequestHandler(
        QLatin1String(Method::RequestPermission),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const RequestPermissionParams p = RequestPermissionParams::fromJson(params);
            if (m_permissionProvider.isNull())
                return resolvedJson(RequestPermissionResult::cancelled().toJson());
            QFuture<RequestPermissionResult> f
                = m_permissionProvider->requestPermission(p.sessionId, p.toolCall, p.options);
            return LLMQore::futureThen(
                this, f, [](const RequestPermissionResult &r) -> QJsonValue { return r.toJson(); });
        });

    // fs/read_text_file
    m_session->setRequestHandler(
        QLatin1String(Method::FsReadTextFile),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (m_fsProvider.isNull())
                return failedJson(Rpc::ErrorCode::MethodNotFound,
                                  QStringLiteral("fs/read_text_file not supported"));
            const ReadTextFileParams p = ReadTextFileParams::fromJson(params);
            QFuture<QString> f
                = m_fsProvider->readTextFile(p.sessionId, p.path, p.line, p.limit);
            return LLMQore::futureThen(this, f, [](const QString &content) -> QJsonValue {
                ReadTextFileResult r;
                r.content = content;
                return r.toJson();
            });
        });

    // fs/write_text_file
    m_session->setRequestHandler(
        QLatin1String(Method::FsWriteTextFile),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (m_fsProvider.isNull() || !m_fsProvider->supportsWrite())
                return failedJson(Rpc::ErrorCode::MethodNotFound,
                                  QStringLiteral("fs/write_text_file not supported"));
            const WriteTextFileParams p = WriteTextFileParams::fromJson(params);
            QFuture<void> f = m_fsProvider->writeTextFile(p.sessionId, p.path, p.content);
            return LLMQore::futureThen(
                this, f, []() -> QJsonValue { return QJsonObject{}; });
        });

    // terminal/create
    m_session->setRequestHandler(
        QLatin1String(Method::TerminalCreate),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (m_terminalProvider.isNull())
                return failedJson(Rpc::ErrorCode::MethodNotFound,
                                  QStringLiteral("terminal not supported"));
            const CreateTerminalParams p = CreateTerminalParams::fromJson(params);
            QFuture<CreateTerminalResult> f = m_terminalProvider->createTerminal(p);
            return LLMQore::futureThen(
                this, f, [](const CreateTerminalResult &r) -> QJsonValue { return r.toJson(); });
        });

    // terminal/output
    m_session->setRequestHandler(
        QLatin1String(Method::TerminalOutput),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (m_terminalProvider.isNull())
                return failedJson(Rpc::ErrorCode::MethodNotFound,
                                  QStringLiteral("terminal not supported"));
            const TerminalOutputParams p = TerminalOutputParams::fromJson(params);
            QFuture<TerminalOutputResult> f
                = m_terminalProvider->terminalOutput(p.sessionId, p.terminalId);
            return LLMQore::futureThen(
                this, f, [](const TerminalOutputResult &r) -> QJsonValue { return r.toJson(); });
        });

    // terminal/wait_for_exit
    m_session->setRequestHandler(
        QLatin1String(Method::TerminalWaitForExit),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (m_terminalProvider.isNull())
                return failedJson(Rpc::ErrorCode::MethodNotFound,
                                  QStringLiteral("terminal not supported"));
            const TerminalRefParams p = TerminalRefParams::fromJson(params);
            QFuture<WaitForTerminalExitResult> f
                = m_terminalProvider->waitForExit(p.sessionId, p.terminalId);
            return LLMQore::futureThen(
                this, f, [](const WaitForTerminalExitResult &r) -> QJsonValue {
                    return r.toJson();
                });
        });

    // terminal/kill
    m_session->setRequestHandler(
        QLatin1String(Method::TerminalKill),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (m_terminalProvider.isNull())
                return failedJson(Rpc::ErrorCode::MethodNotFound,
                                  QStringLiteral("terminal not supported"));
            const TerminalRefParams p = TerminalRefParams::fromJson(params);
            QFuture<void> f = m_terminalProvider->killTerminal(p.sessionId, p.terminalId);
            return LLMQore::futureThen(this, f, []() -> QJsonValue { return QJsonObject{}; });
        });

    // terminal/release
    m_session->setRequestHandler(
        QLatin1String(Method::TerminalRelease),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (m_terminalProvider.isNull())
                return failedJson(Rpc::ErrorCode::MethodNotFound,
                                  QStringLiteral("terminal not supported"));
            const TerminalRefParams p = TerminalRefParams::fromJson(params);
            QFuture<void> f = m_terminalProvider->releaseTerminal(p.sessionId, p.terminalId);
            return LLMQore::futureThen(this, f, []() -> QJsonValue { return QJsonObject{}; });
        });
}

} // namespace LLMQore::Acp
