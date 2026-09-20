// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"

#include <chrono>
#include <optional>
#include <type_traits>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QPromise>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>

#include <LLMQore/AcpClient.hpp>
#include <LLMQore/AcpFileSystemProvider.hpp>
#include <LLMQore/AcpTypes.hpp>
#include <LLMQore/CallbackPermissionProvider.hpp>
#include <LLMQore/RpcExceptions.hpp>
#include <LLMQore/RpcStdioTransport.hpp>

using namespace LLMQore;
using namespace LLMQore::Acp;
using namespace LLMQore::IntegrationTest;

namespace {

constexpr int kAgentStartupTimeoutMs = 90000;
constexpr int kRpcTimeoutMs = 30000;
constexpr int kTurnTimeoutMs = 180000;
constexpr int kSettleSlackMs = 5000;
constexpr int kNotificationDrainMs = 1500;

QString agentProgram()
{
    return getEnvOrDefault("LLMQORE_ACP_AGENT_CMD", QStringLiteral("npx"));
}

QStringList agentArguments()
{
    const QString joined = getEnvOrDefault(
        "LLMQORE_ACP_AGENT_ARGS", QStringLiteral("-y @agentclientprotocol/claude-agent-acp"));
    return joined.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString resolveAgentProgram(const QString &program)
{
    if (program.contains(QLatin1Char('/'))) {
        const QFileInfo info(program);
        return info.isExecutable() ? info.absoluteFilePath() : QString();
    }
    return QStandardPaths::findExecutable(program, childSearchPath());
}

void drainNotifications(int milliseconds = kNotificationDrainMs)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

struct SettleOutcome
{
    bool timedOut = false;
    std::optional<Rpc::RemoteError> remoteError;
    QString failure;

    bool ok() const { return !timedOut && !remoteError.has_value() && failure.isEmpty(); }
};

template<typename T>
void pumpUntilSettled(QFuture<T> &future, int timeoutMs)
{
    if (future.isFinished())
        return;
    QEventLoop loop;
    QFutureWatcher<T> watcher;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
}

SettleOutcome settleVoid(QFuture<void> future, int timeoutMs)
{
    SettleOutcome outcome;
    pumpUntilSettled(future, timeoutMs);
    if (!future.isFinished()) {
        outcome.timedOut = true;
        return outcome;
    }
    try {
        future.waitForFinished();
    } catch (const Rpc::RemoteError &error) {
        outcome.remoteError.emplace(error);
    } catch (const std::exception &error) {
        outcome.failure = QString::fromUtf8(error.what());
    }
    return outcome;
}

template<typename T>
SettleOutcome settleValue(QFuture<T> future, int timeoutMs, T &out)
{
    SettleOutcome outcome;
    pumpUntilSettled(future, timeoutMs);
    if (!future.isFinished()) {
        outcome.timedOut = true;
        return outcome;
    }
    try {
        out = future.result();
    } catch (const Rpc::RemoteError &error) {
        outcome.remoteError.emplace(error);
    } catch (const std::exception &error) {
        outcome.failure = QString::fromUtf8(error.what());
    }
    return outcome;
}

bool looksLikeAuthFailure(const Rpc::RemoteError &error)
{
    if (error.data().toObject().value(QStringLiteral("errorKind")).toString()
        == QLatin1String("authentication_failed"))
        return true;
    return error.remoteMessage().contains(QStringLiteral("authenticat"), Qt::CaseInsensitive);
}

class SandboxFileSystemProvider : public AcpFileSystemProvider
{
public:
    SandboxFileSystemProvider(QString root, QObject *parent = nullptr)
        : AcpFileSystemProvider(parent)
        , m_root(std::move(root))
    {}

    bool supportsWrite() const override { return true; }

    QStringList escapes() const { return m_escapes; }
    int readCount() const { return m_reads; }
    int writeCount() const { return m_writes; }

    QFuture<QString> readTextFile(
        const QString &,
        const QString &path,
        std::optional<int>,
        std::optional<int>) override
    {
        ++m_reads;
        QPromise<QString> promise;
        promise.start();
        if (!isInsideRoot(path)) {
            m_escapes.append(path);
            promise.setException(std::make_exception_ptr(
                Rpc::RemoteError(Rpc::ErrorCode::InvalidParams, refusal(path))));
        } else {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                promise.setException(std::make_exception_ptr(Rpc::RemoteError(
                    Rpc::ErrorCode::InternalError, QStringLiteral("cannot open ") + path)));
            } else {
                promise.addResult(QString::fromUtf8(file.readAll()));
            }
        }
        promise.finish();
        return promise.future();
    }

    QFuture<void> writeTextFile(
        const QString &, const QString &path, const QString &content) override
    {
        ++m_writes;
        QPromise<void> promise;
        promise.start();
        if (!isInsideRoot(path)) {
            m_escapes.append(path);
            promise.setException(std::make_exception_ptr(
                Rpc::RemoteError(Rpc::ErrorCode::InvalidParams, refusal(path))));
        } else {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                promise.setException(std::make_exception_ptr(Rpc::RemoteError(
                    Rpc::ErrorCode::InternalError, QStringLiteral("cannot write ") + path)));
            } else {
                file.write(content.toUtf8());
            }
        }
        promise.finish();
        return promise.future();
    }

    bool isInsideRoot(const QString &path) const
    {
        const QString root = QFileInfo(m_root).canonicalFilePath();
        if (root.isEmpty())
            return false;
        QString candidate = QFileInfo(path).canonicalFilePath();
        if (candidate.isEmpty())
            candidate = QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath();
        if (candidate.isEmpty())
            return false;
        return candidate == root || candidate.startsWith(root + QLatin1Char('/'));
    }

private:
    QString refusal(const QString &path) const
    {
        return QStringLiteral("path outside the test sandbox: ") + path;
    }

    QString m_root;
    QStringList m_escapes;
    int m_reads = 0;
    int m_writes = 0;
};

struct RecordedPermission
{
    QString sessionId;
    ToolCall toolCall;
    QList<PermissionOption> options;
    bool refusedForSandboxEscape = false;
};

} // namespace

class AcpIntegrationTest : public ProviderTestBase
{
protected:
    static void SetUpTestSuite();
    static void TearDownTestSuite();

    void SetUp() override
    {
        ProviderTestBase::SetUp();
        if (!s_skipReason.isEmpty())
            GTEST_SKIP() << s_skipReason.toStdString();
        s_permissions.clear();
        s_stderr.clear();
    }

    static NewSessionResult openSession();
    static std::string agentDiagnostics();
    static std::string describe(const SettleOutcome &outcome);
    static RequestPermissionResult decidePermission(
        const QString &sessionId,
        const ToolCall &toolCall,
        const QList<PermissionOption> &options);

    static QTemporaryDir *s_sandbox;
    static Rpc::StdioClientTransport *s_transport;
    static AcpClient *s_client;
    static SandboxFileSystemProvider *s_fs;
    static InitializeResult s_init;
    static QString s_resolvedProgram;
    static QString s_skipReason;
    static QList<RecordedPermission> s_permissions;
    static QStringList s_stderr;
};

QTemporaryDir *AcpIntegrationTest::s_sandbox = nullptr;
Rpc::StdioClientTransport *AcpIntegrationTest::s_transport = nullptr;
AcpClient *AcpIntegrationTest::s_client = nullptr;
SandboxFileSystemProvider *AcpIntegrationTest::s_fs = nullptr;
InitializeResult AcpIntegrationTest::s_init = {};
QString AcpIntegrationTest::s_resolvedProgram;
QString AcpIntegrationTest::s_skipReason;
QList<RecordedPermission> AcpIntegrationTest::s_permissions;
QStringList AcpIntegrationTest::s_stderr;

RequestPermissionResult AcpIntegrationTest::decidePermission(
    const QString &sessionId, const ToolCall &toolCall, const QList<PermissionOption> &options)
{
    RecordedPermission recorded{sessionId, toolCall, options, false};

    for (const ToolCallLocation &location : toolCall.locations) {
        if (!s_fs->isInsideRoot(location.path)) {
            recorded.refusedForSandboxEscape = true;
            s_permissions.append(recorded);
            return RequestPermissionResult::cancelled();
        }
    }

    s_permissions.append(recorded);

    for (const PermissionOption &option : options) {
        if (option.kind == QLatin1String(PermissionOptionKind::AllowOnce))
            return RequestPermissionResult::selected(option.optionId);
    }
    if (!options.isEmpty())
        return RequestPermissionResult::selected(options.first().optionId);
    return RequestPermissionResult::cancelled();
}

std::string AcpIntegrationTest::agentDiagnostics()
{
    QString text = QStringLiteral("  agent:      ");
    if (s_init.agentInfo)
        text += QString("%1 %2").arg(s_init.agentInfo->name, s_init.agentInfo->version);
    else
        text += QStringLiteral("(no agentInfo)");
    text += QString("\n  launched:   %1 %2\n")
                .arg(s_resolvedProgram.isEmpty() ? agentProgram() : s_resolvedProgram,
                     agentArguments().join(QLatin1Char(' ')));
    if (!s_stderr.isEmpty())
        text += QString("  stderr:     %1\n").arg(s_stderr.last().left(300));
    return text.toStdString();
}

std::string AcpIntegrationTest::describe(const SettleOutcome &outcome)
{
    QString text;
    if (outcome.timedOut)
        text += QStringLiteral("  timed out waiting for the agent\n");
    if (outcome.remoteError) {
        text += QString("  rpc error:  code=%1 message='%2'\n")
                    .arg(outcome.remoteError->code())
                    .arg(outcome.remoteError->remoteMessage());
        const QJsonObject data = outcome.remoteError->data().toObject();
        if (!data.isEmpty()) {
            text += QString("  data:       %1\n")
                        .arg(QString::fromUtf8(
                            QJsonDocument(data).toJson(QJsonDocument::Compact)));
        }
        if (looksLikeAuthFailure(*outcome.remoteError)) {
            text += QStringLiteral(
                "  The agent authenticates from its own environment, not from ACP.\n"
                "  Run `claude setup-token` and export CLAUDE_CODE_OAUTH_TOKEN where a\n"
                "  non-interactive shell can see it (~/.zshenv, not ~/.zshrc).\n"
                "  See docs/acp/authentication.md.\n");
        }
    }
    if (!outcome.failure.isEmpty())
        text += QString("  failure:    %1\n").arg(outcome.failure);
    return text.toStdString() + agentDiagnostics();
}

void AcpIntegrationTest::SetUpTestSuite()
{
    if (!QCoreApplication::instance()) {
        s_skipReason = QStringLiteral("no QCoreApplication instance");
        return;
    }

    s_sandbox = new QTemporaryDir;
    if (!s_sandbox->isValid()) {
        s_skipReason = QStringLiteral("cannot create a temporary sandbox directory");
        return;
    }

    const QString requested = agentProgram();
    s_resolvedProgram = resolveAgentProgram(requested);
    if (s_resolvedProgram.isEmpty()) {
        s_skipReason = QString("ACP agent program '%1' was not found.\n"
                               "  search path: %2\n"
                               "  A GUI launcher does not inherit your shell PATH. Add the\n"
                               "  directory holding it to LLMQORE_ENV_PATH in the run\n"
                               "  configuration's environment.")
                           .arg(requested, childSearchPath().join(QLatin1Char(':')));
        return;
    }

    Rpc::StdioLaunchConfig config;
    config.program = s_resolvedProgram;
    config.arguments = agentArguments();
    config.workingDirectory = s_sandbox->path();
    config.startupTimeoutMs = 30000;
    config.environment = childEnvironment();

    s_transport = new Rpc::StdioClientTransport(config);
    s_client = new AcpClient(
        s_transport, Implementation{QStringLiteral("LLMQore"), QStringLiteral("0.7.0"),
                                    QStringLiteral("LLMQore")});
    s_fs = new SandboxFileSystemProvider(s_sandbox->path(), s_client);
    s_client->setFileSystemProvider(s_fs);
    s_client->setPermissionProvider(new CallbackPermissionProvider(decidePermission, s_client));

    QObject::connect(s_client, &AcpClient::agentStderr, s_client, [](const QString &line) {
        s_stderr.append(line);
    });

    const SettleOutcome outcome = settleValue(
        s_client->connectAndInitialize(std::chrono::milliseconds(kAgentStartupTimeoutMs)),
        kAgentStartupTimeoutMs + kSettleSlackMs,
        s_init);

    if (!outcome.ok()) {
        s_skipReason = QString("ACP agent '%1 %2' did not start:\n%3")
                           .arg(config.program,
                                config.arguments.join(QLatin1Char(' ')),
                                QString::fromStdString(describe(outcome)));
    }
}

void AcpIntegrationTest::TearDownTestSuite()
{
    if (s_client) {
        s_client->shutdown();
        delete s_client;
        s_client = nullptr;
    }
    s_fs = nullptr;
    delete s_transport;
    s_transport = nullptr;
    delete s_sandbox;
    s_sandbox = nullptr;
}

NewSessionResult AcpIntegrationTest::openSession()
{
    NewSessionParams params;
    params.cwd = s_sandbox->path();

    NewSessionResult result;
    const SettleOutcome outcome = settleValue(
        s_client->newSession(params, std::chrono::milliseconds(kRpcTimeoutMs)),
        kRpcTimeoutMs + kSettleSlackMs,
        result);
    EXPECT_TRUE(outcome.ok()) << "session/new failed\n" << describe(outcome);
    return result;
}

TEST_F(AcpIntegrationTest, InitializeNegotiatesProtocolAndCapabilities)
{
    EXPECT_EQ(s_init.protocolVersion, kAcpProtocolVersion) << agentDiagnostics();
    EXPECT_TRUE(s_init.authMethods.isEmpty())
        << "agent advertises authMethods; the host would need an authenticate step\n"
        << agentDiagnostics();

    ASSERT_TRUE(s_init.agentInfo.has_value()) << agentDiagnostics();
    EXPECT_FALSE(s_init.agentInfo->name.isEmpty()) << agentDiagnostics();
    EXPECT_FALSE(s_init.agentInfo->version.isEmpty()) << agentDiagnostics();

    EXPECT_TRUE(s_init.agentCapabilities.loadSession) << agentDiagnostics();
    EXPECT_TRUE(s_init.agentCapabilities.mcpCapabilities.http) << agentDiagnostics();
    EXPECT_TRUE(s_init.agentCapabilities.mcpCapabilities.sse) << agentDiagnostics();
    EXPECT_TRUE(s_init.agentCapabilities.promptCapabilities.image) << agentDiagnostics();

    EXPECT_TRUE(s_client->isInitialized());
    EXPECT_TRUE(s_client->clientCapabilities().fs.readTextFile);
    EXPECT_TRUE(s_client->clientCapabilities().fs.writeTextFile);
}

TEST_F(AcpIntegrationTest, NewSessionRegistersSessionAndReportsModes)
{
    QStringList commandNames;
    QString commandsSession;
    const QMetaObject::Connection connection = QObject::connect(
        s_client,
        &AcpClient::availableCommandsUpdated,
        s_client,
        [&](const QString &sessionId, const QList<AvailableCommand> &commands) {
            commandsSession = sessionId;
            commandNames.clear();
            for (const AvailableCommand &command : commands)
                commandNames.append(command.name);
        });

    const NewSessionResult session = openSession();
    drainNotifications();
    QObject::disconnect(connection);

    ASSERT_FALSE(session.sessionId.isEmpty()) << agentDiagnostics();
    EXPECT_TRUE(s_client->sessionIds().contains(session.sessionId)) << agentDiagnostics();

    ASSERT_TRUE(session.modes.has_value()) << "agent reported no session modes\n"
                                           << agentDiagnostics();
    EXPECT_EQ(session.modes->currentModeId, QStringLiteral("default"))
        << "a session that does not start in 'default' will not ask for permission\n"
        << agentDiagnostics();

    QStringList modeIds;
    for (const SessionMode &mode : session.modes->availableModes)
        modeIds.append(mode.id);
    EXPECT_TRUE(modeIds.contains(QStringLiteral("default")))
        << "modes: " << modeIds.join(QLatin1Char(',')).toStdString();
    EXPECT_TRUE(modeIds.contains(QStringLiteral("bypassPermissions")))
        << "modes: " << modeIds.join(QLatin1Char(',')).toStdString();

    EXPECT_EQ(commandsSession, session.sessionId)
        << "no available_commands_update arrived for the new session\n"
        << agentDiagnostics();
    EXPECT_FALSE(commandNames.isEmpty()) << agentDiagnostics();
}

TEST_F(AcpIntegrationTest, NewSessionReturnsConfigOptions)
{
    const NewSessionResult session = openSession();
    ASSERT_FALSE(session.sessionId.isEmpty()) << agentDiagnostics();

    ASSERT_FALSE(session.configOptions.isEmpty()) << "session/new returned no configOptions\n"
                                                  << agentDiagnostics();

    for (const SessionConfigOption &option : session.configOptions) {
        EXPECT_FALSE(option.id.isEmpty()) << agentDiagnostics();
        EXPECT_FALSE(option.type.isEmpty())
            << "option " << option.id.toStdString() << " has no type\n"
            << agentDiagnostics();
    }
}

TEST_F(AcpIntegrationTest, SetConfigOptionSwitchesModeAndNotifies)
{
    const NewSessionResult session = openSession();
    drainNotifications();
    ASSERT_FALSE(session.sessionId.isEmpty()) << agentDiagnostics();

    const SessionConfigOption *modeOption = nullptr;
    for (const SessionConfigOption &option : session.configOptions) {
        if (option.category == QLatin1String("mode"))
            modeOption = &option;
    }
    ASSERT_NE(modeOption, nullptr) << "no configOption with category \"mode\"\n"
                                   << agentDiagnostics();

    QString target;
    for (const SessionConfigSelectOption &choice : modeOption->options) {
        if (choice.value != modeOption->value) {
            target = choice.value;
            break;
        }
    }
    ASSERT_FALSE(target.isEmpty()) << "mode option has no alternative value\n"
                                   << agentDiagnostics();

    QString changedSession;
    QString changedMode;
    const QMetaObject::Connection connection = QObject::connect(
        s_client,
        &AcpClient::modeChanged,
        s_client,
        [&](const QString &sessionId, const QString &modeId) {
            changedSession = sessionId;
            changedMode = modeId;
        });

    QList<SessionConfigOption> reply;
    const SettleOutcome outcome = settleValue(
        s_client->setConfigOption(
            session.sessionId, modeOption->id, target, std::chrono::milliseconds(kRpcTimeoutMs)),
        kRpcTimeoutMs + kSettleSlackMs,
        reply);
    drainNotifications();
    QObject::disconnect(connection);

    ASSERT_TRUE(outcome.ok()) << "session/set_config_option was rejected\n" << describe(outcome);

    bool found = false;
    for (const SessionConfigOption &option : reply) {
        if (option.id == modeOption->id) {
            found = true;
            EXPECT_EQ(option.value, target) << agentDiagnostics();
        }
    }
    EXPECT_TRUE(found) << "response did not include the mode option\n" << agentDiagnostics();

    EXPECT_EQ(changedSession, session.sessionId)
        << "no current_mode_update arrived after set_config_option\n"
        << agentDiagnostics();
    EXPECT_EQ(changedMode, target) << agentDiagnostics();
}

TEST_F(AcpIntegrationTest, SetModeIsAcceptedByTheAgent)
{
    const NewSessionResult session = openSession();
    ASSERT_FALSE(session.sessionId.isEmpty()) << agentDiagnostics();

    const SettleOutcome outcome = settleVoid(
        s_client->setMode(
            session.sessionId, QStringLiteral("plan"), std::chrono::milliseconds(kRpcTimeoutMs)),
        kRpcTimeoutMs + kSettleSlackMs);

    EXPECT_TRUE(outcome.ok()) << "session/set_mode was rejected\n" << describe(outcome);
}

TEST_F(AcpIntegrationTest, SetModeTriggersConfigOptionsUpdated)
{
    const NewSessionResult session = openSession();
    drainNotifications();
    ASSERT_FALSE(session.sessionId.isEmpty()) << agentDiagnostics();

    QString updatedSession;
    QList<SessionConfigOption> updatedOptions;
    const QMetaObject::Connection connection = QObject::connect(
        s_client,
        &AcpClient::configOptionsUpdated,
        s_client,
        [&](const QString &sessionId, const QList<SessionConfigOption> &options) {
            updatedSession = sessionId;
            updatedOptions = options;
        });

    const SettleOutcome outcome = settleVoid(
        s_client->setMode(
            session.sessionId, QStringLiteral("plan"), std::chrono::milliseconds(kRpcTimeoutMs)),
        kRpcTimeoutMs + kSettleSlackMs);
    drainNotifications();
    QObject::disconnect(connection);

    ASSERT_TRUE(outcome.ok()) << "session/set_mode was rejected\n" << describe(outcome);

    EXPECT_EQ(updatedSession, session.sessionId)
        << "no config_option_update arrived after set_mode\n"
        << agentDiagnostics();

    bool found = false;
    for (const SessionConfigOption &option : updatedOptions) {
        if (option.category == QLatin1String("mode")) {
            found = true;
            EXPECT_EQ(option.value, QStringLiteral("plan")) << agentDiagnostics();
        }
    }
    EXPECT_TRUE(found) << "config_option_update did not include the mode option\n"
                       << agentDiagnostics();
}

TEST_F(AcpIntegrationTest, PromptStreamsChunksAndEndsTurn)
{
    const NewSessionResult session = openSession();
    ASSERT_FALSE(session.sessionId.isEmpty()) << agentDiagnostics();

    QString streamed;
    int usageUpdates = 0;
    const QMetaObject::Connection chunks = QObject::connect(
        s_client, &AcpClient::agentMessageChunk, s_client,
        [&](const QString &, const Acp::ContentBlock &block) { streamed += block.text; });
    const QMetaObject::Connection usage = QObject::connect(
        s_client, &AcpClient::usageUpdated, s_client,
        [&](const QString &, const QJsonObject &) { ++usageUpdates; });

    PromptResult result;
    const SettleOutcome outcome = settleValue(
        s_client->prompt(
            session.sessionId,
            {Acp::ContentBlock::makeText(QStringLiteral(
                "Reply with exactly the two letters OK and nothing else. "
                "Do not use any tools."))},
            std::chrono::milliseconds(kTurnTimeoutMs)),
        kTurnTimeoutMs + kSettleSlackMs,
        result);

    QObject::disconnect(chunks);
    QObject::disconnect(usage);

    ASSERT_TRUE(outcome.ok()) << "session/prompt failed\n" << describe(outcome);
    EXPECT_EQ(result.stopReason, QString::fromLatin1(StopReason::EndTurn))
        << agentDiagnostics();
    EXPECT_FALSE(result.usage.isEmpty()) << "PromptResult carried no usage\n"
                                         << agentDiagnostics();
    EXPECT_FALSE(streamed.trimmed().isEmpty())
        << "no agent_message_chunk carried text\n" << agentDiagnostics();
    EXPECT_GT(usageUpdates, 0) << "no usage_update arrived during the turn\n"
                               << agentDiagnostics();
}

TEST_F(AcpIntegrationTest, CancelDuringStreamingEndsTurnCancelled)
{
    const NewSessionResult session = openSession();
    ASSERT_FALSE(session.sessionId.isEmpty()) << agentDiagnostics();

    bool cancelSent = false;
    const QMetaObject::Connection chunks = QObject::connect(
        s_client, &AcpClient::agentMessageChunk, s_client,
        [&](const QString &sessionId, const Acp::ContentBlock &block) {
            if (cancelSent || block.text.trimmed().isEmpty())
                return;
            cancelSent = true;
            s_client->cancel(sessionId);
        });

    PromptResult result;
    const SettleOutcome outcome = settleValue(
        s_client->prompt(
            session.sessionId,
            {Acp::ContentBlock::makeText(QStringLiteral(
                "Write a 600 word description of a lighthouse keeper's daily routine. "
                "Write it as continuous prose. Do not use any tools."))},
            std::chrono::milliseconds(kTurnTimeoutMs)),
        kTurnTimeoutMs + kSettleSlackMs,
        result);

    QObject::disconnect(chunks);

    ASSERT_TRUE(outcome.ok()) << "session/prompt failed\n" << describe(outcome);
    ASSERT_TRUE(cancelSent) << "no agent_message_chunk arrived, cancel was never sent\n"
                            << agentDiagnostics();
    EXPECT_EQ(result.stopReason, QString::fromLatin1(StopReason::Cancelled))
        << "the turn outran the cancel; lengthen the prompt rather than weakening this\n"
        << agentDiagnostics();
}

TEST_F(AcpIntegrationTest, ToolTurnRequestsPermissionAndMergesToolCalls)
{
    const QDir sandbox(s_sandbox->path());
    const QString inputPath = sandbox.filePath(QStringLiteral("input.txt"));
    const QString outputPath = sandbox.filePath(QStringLiteral("output.txt"));
    QFile::remove(outputPath);
    {
        QFile input(inputPath);
        ASSERT_TRUE(input.open(QIODevice::WriteOnly | QIODevice::Truncate));
        input.write("hello from host\n");
    }

    const NewSessionResult session = openSession();
    ASSERT_FALSE(session.sessionId.isEmpty()) << agentDiagnostics();

    QHash<QString, ToolCall> merged;
    const QMetaObject::Connection started = QObject::connect(
        s_client, &AcpClient::toolCallStarted, s_client,
        [&](const QString &, const ToolCall &call) { merged.insert(call.toolCallId, call); });
    const QMetaObject::Connection updated = QObject::connect(
        s_client, &AcpClient::toolCallUpdated, s_client,
        [&](const QString &, const ToolCall &call) { merged.insert(call.toolCallId, call); });

    PromptResult result;
    const SettleOutcome outcome = settleValue(
        s_client->prompt(
            session.sessionId,
            {Acp::ContentBlock::makeText(QStringLiteral(
                "Read the file input.txt in the current directory, then write its "
                "contents upper-cased into a new file output.txt in the same "
                "directory. Use your file reading and file writing tools for both "
                "steps — do not run any shell command. Do not ask me anything, "
                "just do it."))},
            std::chrono::milliseconds(kTurnTimeoutMs)),
        kTurnTimeoutMs + kSettleSlackMs,
        result);

    QObject::disconnect(started);
    QObject::disconnect(updated);

    QString toolDiagnostics;
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
        toolDiagnostics += QString("  tool: id=%1 status='%2' kind='%3' title='%4' rawInput=%5\n")
                               .arg(it.key(), it->status, it->kind, it->title)
                               .arg(it->rawInput.isEmpty() ? 0 : it->rawInput.size());
    }
    const std::string context = toolDiagnostics.toStdString() + agentDiagnostics();

    ASSERT_TRUE(outcome.ok()) << "session/prompt failed\n" << describe(outcome);
    EXPECT_EQ(result.stopReason, QString::fromLatin1(StopReason::EndTurn)) << context;

    ASSERT_FALSE(s_permissions.isEmpty())
        << "the turn produced no session/request_permission; the agent may have taken a "
           "shell route that needs no approval\n"
        << context;

    QStringList approvedToolCallIds;
    for (const RecordedPermission &permission : s_permissions) {
        EXPECT_FALSE(permission.refusedForSandboxEscape)
            << "the agent asked to touch a path outside the sandbox\n" << context;
        EXPECT_EQ(permission.sessionId, session.sessionId) << context;
        ASSERT_FALSE(permission.options.isEmpty())
            << "permission request carried no options\n" << context;

        bool sawAllowOnce = false;
        for (const PermissionOption &option : permission.options) {
            EXPECT_FALSE(option.optionId.isEmpty()) << context;
            if (option.kind == QLatin1String(PermissionOptionKind::AllowOnce))
                sawAllowOnce = true;
        }
        EXPECT_TRUE(sawAllowOnce) << "no allow_once option offered\n" << context;

        ASSERT_FALSE(permission.toolCall.toolCallId.isEmpty()) << context;
        EXPECT_FALSE(approvedToolCallIds.contains(permission.toolCall.toolCallId))
            << "the same tool call asked for permission twice\n" << context;
        approvedToolCallIds.append(permission.toolCall.toolCallId);

        EXPECT_FALSE(permission.toolCall.locations.isEmpty())
            << "permission request carried no locations to check against the sandbox\n"
            << context;
        for (const ToolCallLocation &location : permission.toolCall.locations)
            EXPECT_TRUE(s_fs->isInsideRoot(location.path)) << location.path.toStdString();
    }

    EXPECT_TRUE(s_fs->escapes().isEmpty())
        << "the file system provider refused: " << s_fs->escapes().join(QLatin1Char(',')).toStdString();

    bool sawCompletedMerge = false;
    for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
        if (it->status == QLatin1String("completed") && !it->title.isEmpty()
            && !it->kind.isEmpty() && !it->rawInput.isEmpty()) {
            sawCompletedMerge = true;
            break;
        }
    }
    EXPECT_TRUE(sawCompletedMerge)
        << "no tool call ended completed with title and rawInput preserved across "
           "partial tool_call_update frames\n"
        << context;

    EXPECT_TRUE(QFile::exists(outputPath))
        << "the turn did not execute far enough to write the file\n" << context;
}
