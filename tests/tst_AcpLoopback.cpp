// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <LLMQore/AcpClient.hpp>
#include <LLMQore/CallbackPermissionProvider.hpp>
#include <LLMQore/DefaultFileSystemProvider.hpp>
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/RpcPipeTransport.hpp>

#include "TestHelpers.hpp"

using namespace LLMQore;
using namespace LLMQore::Acp;

namespace {

QFuture<QJsonValue> resolvedJson(const QJsonValue &v)
{
    auto p = std::make_shared<QPromise<QJsonValue>>();
    p->start();
    p->addResult(v);
    p->finish();
    return p->future();
}

class FakeAgent
{
public:
    explicit FakeAgent(Rpc::Transport *transport)
        : m_session(new Rpc::JsonRpcSession(transport))
    {
        install();
    }
    ~FakeAgent() { delete m_session; }

    Rpc::JsonRpcSession *session() const { return m_session; }

    InitializeResult initResult;
    QString sessionId = QStringLiteral("sess-A");
    QStringList agentChunks;
    QString stopReason = QString::fromLatin1(StopReason::EndTurn);

private:
    void install()
    {
        m_session->setRequestHandler(
            QLatin1String(Method::Initialize),
            [this](const QJsonObject &) { return resolvedJson(initResult.toJson()); });

        m_session->setRequestHandler(
            QLatin1String(Method::NewSession), [this](const QJsonObject &) {
                NewSessionResult r;
                r.sessionId = sessionId;
                return resolvedJson(r.toJson());
            });

        m_session->setRequestHandler(
            QLatin1String(Method::Prompt),
            [this](const QJsonObject &params) -> QFuture<QJsonValue> {
                const QString sid = params.value("sessionId").toString();
                for (const QString &c : agentChunks) {
                    SessionNotification n;
                    n.sessionId = sid;
                    n.update.sessionUpdate
                        = QString::fromLatin1(SessionUpdateKind::AgentMessageChunk);
                    n.update.content = ContentBlock::makeText(c);
                    m_session->sendNotification(
                        QLatin1String(Method::SessionUpdate), n.toJson());
                }
                PromptResult pr;
                pr.stopReason = stopReason;
                return resolvedJson(pr.toJson());
            });
    }

    Rpc::JsonRpcSession *m_session;
};

class AcpLoopbackTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_AcpLoopback";
            static char *argv[] = {arg0};
            m_app = new QCoreApplication(argc, argv);
        }
    }
    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }
    QCoreApplication *m_app = nullptr;
};

} // namespace

TEST_F(AcpLoopbackTest, InitializeNegotiatesCapabilities)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    FakeAgent agent(serverTransport);
    agent.initResult.protocolVersion = kAcpProtocolVersion;
    agent.initResult.agentCapabilities.loadSession = true;
    agent.initResult.agentCapabilities.promptCapabilities.image = true;

    serverTransport->start();

    AcpClient client(clientTransport);
    const InitializeResult init = waitForFuture(client.connectAndInitialize());
    EXPECT_EQ(init.protocolVersion, kAcpProtocolVersion);
    EXPECT_TRUE(init.agentCapabilities.loadSession);
    EXPECT_TRUE(init.agentCapabilities.promptCapabilities.image);
    EXPECT_TRUE(client.isInitialized());

    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, ClientAdvertisesCapabilitiesFromProviders)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    FakeAgent agent(serverTransport);
    serverTransport->start();

    AcpClient client(clientTransport);
    // No providers -> nothing advertised.
    EXPECT_FALSE(client.clientCapabilities().fs.readTextFile);
    EXPECT_FALSE(client.clientCapabilities().terminal);

    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, BooleanConfigOptionsCapabilityIsOnByDefaultAndOmittedWhenOff)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    FakeAgent agent(serverTransport);
    serverTransport->start();

    AcpClient client(clientTransport);

    const QJsonObject on = client.clientCapabilities().toJson();
    const QJsonObject expectedOn{{"configOptions", QJsonObject{{"boolean", QJsonObject{}}}}};
    EXPECT_EQ(on.value("session").toObject(), expectedOn);

    client.setBooleanConfigOptionsSupported(false);
    const QJsonObject off = client.clientCapabilities().toJson();
    EXPECT_FALSE(
        off.value("session").toObject().value("configOptions").toObject().contains("boolean"));

    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, NewSessionRegistersSessionId)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    FakeAgent agent(serverTransport);
    agent.sessionId = "sess-XYZ";
    serverTransport->start();

    AcpClient client(clientTransport);
    waitForFuture(client.connectAndInitialize());

    NewSessionParams params;
    params.cwd = "/home/user/project";
    const NewSessionResult ns = waitForFuture(client.newSession(params));
    EXPECT_EQ(ns.sessionId, "sess-XYZ");
    ASSERT_EQ(client.sessionIds().size(), 1);
    EXPECT_EQ(client.sessionIds().first(), "sess-XYZ");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, SetConfigOptionRoundTripsAndUpdatesArrive)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    FakeAgent agent(serverTransport);
    serverTransport->start();

    QJsonObject seen;
    agent.session()->setRequestHandler(
        QLatin1String(Method::SetConfigOption), [&](const QJsonObject &params) {
            seen = params;
            SessionConfigOption model;
            model.id = "model";
            model.name = "Model";
            model.type = "select";
            model.value = params.value("value").toString();
            model.options.append(SessionConfigSelectOption{"default", "Default", ""});
            model.options.append(SessionConfigSelectOption{"sonnet", "Sonnet", ""});

            SessionNotification n;
            n.sessionId = params.value("sessionId").toString();
            n.update.sessionUpdate
                = QString::fromLatin1(SessionUpdateKind::ConfigOptionUpdate);
            n.update.configOptions = {model};
            agent.session()->sendNotification(
                QLatin1String(Method::SessionUpdate), n.toJson());

            return resolvedJson(QJsonObject{{"configOptions", configOptionsToJson({model})}});
        });

    AcpClient client(clientTransport);
    waitForFuture(client.connectAndInitialize());
    const NewSessionResult ns = waitForFuture(client.newSession(NewSessionParams{}));

    QString updateSession;
    QList<SessionConfigOption> updated;
    QObject::connect(
        &client,
        &AcpClient::configOptionsUpdated,
        [&](const QString &sid, const QList<SessionConfigOption> &options) {
            updateSession = sid;
            updated = options;
        });

    const QList<SessionConfigOption> refreshed
        = waitForFuture(client.setConfigOption(ns.sessionId, "model", "sonnet"));

    // SetSessionConfigOptionRequest, string arm: no "type" key.
    const QJsonObject
        expectedRequest{{"sessionId", ns.sessionId}, {"configId", "model"}, {"value", "sonnet"}};
    EXPECT_EQ(seen, expectedRequest);

    ASSERT_EQ(refreshed.size(), 1);
    EXPECT_EQ(refreshed.first().value, "sonnet");
    ASSERT_EQ(refreshed.first().options.size(), 2);

    EXPECT_EQ(updateSession, ns.sessionId);
    ASSERT_EQ(updated.size(), 1);
    EXPECT_EQ(updated.first().id, "model");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, BooleanConfigValueCarriesItsTypeTag)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    FakeAgent agent(serverTransport);
    serverTransport->start();

    QJsonObject seen;
    agent.session()->setRequestHandler(
        QLatin1String(Method::SetConfigOption), [&](const QJsonObject &params) {
            seen = params;
            return resolvedJson(QJsonObject{{"configOptions", QJsonArray{}}});
        });

    AcpClient client(clientTransport);
    waitForFuture(client.connectAndInitialize());
    const NewSessionResult ns = waitForFuture(client.newSession(NewSessionParams{}));
    waitForFuture(client.setConfigOption(ns.sessionId, "fast", true));

    // SetSessionConfigOptionRequest, boolean arm: "type" is required.
    const QJsonObject expectedRequest{
        {"sessionId", ns.sessionId}, {"configId", "fast"}, {"value", true}, {"type", "boolean"}};
    EXPECT_EQ(seen, expectedRequest);

    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, StreamingPromptDeliversChunksThenStopReason)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    FakeAgent agent(serverTransport);
    agent.agentChunks = QStringList{"Hello, ", "world", "!"};
    agent.stopReason = QString::fromLatin1(StopReason::EndTurn);
    serverTransport->start();

    AcpClient client(clientTransport);
    waitForFuture(client.connectAndInitialize());
    const NewSessionResult ns = waitForFuture(client.newSession(NewSessionParams{}));

    auto buffer = std::make_shared<QString>();
    QObject::connect(
        &client,
        &AcpClient::agentMessageChunk,
        [buffer](const QString &, const ContentBlock &c) { buffer->append(c.text); });

    QString finishedSession;
    QString finishedReason;
    QObject::connect(
        &client, &AcpClient::promptFinished, [&](const QString &sid, const QString &reason) {
            finishedSession = sid;
            finishedReason = reason;
        });

    const PromptResult pr
        = waitForFuture(client.prompt(ns.sessionId, {ContentBlock::makeText("hi")}));

    EXPECT_EQ(pr.stopReason, "end_turn");
    EXPECT_EQ(*buffer, "Hello, world!");
    EXPECT_EQ(finishedSession, ns.sessionId);
    EXPECT_EQ(finishedReason, "end_turn");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, CancelSendsNotificationAndTurnEndsCancelled)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    // Agent whose prompt parks until it sees a session/cancel notification,
    // then resolves with stopReason "Cancelled".
    auto *agentSession = new Rpc::JsonRpcSession(serverTransport);
    agentSession->setRequestHandler(
        QLatin1String(Method::Initialize),
        [](const QJsonObject &) { return resolvedJson(InitializeResult{}.toJson()); });
    agentSession->setRequestHandler(
        QLatin1String(Method::NewSession), [](const QJsonObject &) {
            NewSessionResult r;
            r.sessionId = "s";
            return resolvedJson(r.toJson());
        });

    auto promptPromise = std::make_shared<QPromise<QJsonValue>>();
    agentSession->setRequestHandler(
        QLatin1String(Method::Prompt),
        [promptPromise](const QJsonObject &) -> QFuture<QJsonValue> {
            promptPromise->start();
            return promptPromise->future();
        });
    agentSession->setNotificationHandler(
        QLatin1String(Method::Cancel), [promptPromise](const QJsonObject &) {
            PromptResult pr;
            pr.stopReason = QString::fromLatin1(StopReason::Cancelled);
            promptPromise->addResult(pr.toJson());
            promptPromise->finish();
        });

    serverTransport->start();

    AcpClient client(clientTransport);
    waitForFuture(client.connectAndInitialize());
    const NewSessionResult ns = waitForFuture(client.newSession(NewSessionParams{}));

    QFuture<PromptResult> promptFuture
        = client.prompt(ns.sessionId, {ContentBlock::makeText("do work")});

    // Let the prompt reach the agent, then cancel.
    QEventLoop pump;
    QTimer::singleShot(50, &pump, &QEventLoop::quit);
    pump.exec();

    client.cancel(ns.sessionId);

    const PromptResult pr = waitForFuture(promptFuture);
    EXPECT_EQ(pr.stopReason, "cancelled");

    delete agentSession;
    delete serverTransport;
    delete clientTransport;
}

// End-to-end: during a prompt the agent calls back into the host for
// fs/read_text_file and session/request_permission (served by real providers),
// streams a chunk echoing both, then completes.
TEST_F(AcpLoopbackTest, PromptDrivesHostCallbacksFsAndPermission)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString filePath = dir.filePath("input.txt");
    {
        QFile f(filePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("hello-from-host");
        f.close();
    }

    auto *agentSession = new Rpc::JsonRpcSession(serverTransport);
    agentSession->setRequestHandler(
        QLatin1String(Method::Initialize),
        [](const QJsonObject &) { return resolvedJson(InitializeResult{}.toJson()); });
    agentSession->setRequestHandler(
        QLatin1String(Method::NewSession), [](const QJsonObject &) {
            NewSessionResult r;
            r.sessionId = "s";
            return resolvedJson(r.toJson());
        });

    auto capturedContent = std::make_shared<QString>();
    auto capturedOutcome = std::make_shared<QString>();

    agentSession->setRequestHandler(
        QLatin1String(Method::Prompt),
        [agentSession, filePath, capturedContent, capturedOutcome](
            const QJsonObject &params) -> QFuture<QJsonValue> {
            const QString sid = params.value("sessionId").toString();

            ReadTextFileParams rp;
            rp.sessionId = sid;
            rp.path = filePath;
            QFuture<QJsonValue> readF = agentSession->sendRequest(
                QLatin1String(Method::FsReadTextFile), rp.toJson());

            return LLMQore::compat(readF)
                .then(
                    agentSession,
                    [agentSession, sid, capturedContent](
                        const QJsonValue &rv) -> QFuture<QJsonValue> {
                        *capturedContent = ReadTextFileResult::fromJson(rv.toObject()).content;
                        RequestPermissionParams pp;
                        pp.sessionId = sid;
                        pp.toolCall.toolCallId = "c1";
                        pp.toolCall.title = "write file";
                        pp.options.append(
                            PermissionOption{"ok", "Allow", PermissionOptionKind::AllowOnce});
                        return agentSession->sendRequest(
                            QLatin1String(Method::RequestPermission), pp.toJson());
                    })
                .unwrap()
                .then(
                    agentSession,
                    [agentSession, sid, capturedContent, capturedOutcome](
                        const QJsonValue &pv) -> QJsonValue {
                        const RequestPermissionResult r
                            = RequestPermissionResult::fromJson(pv.toObject());
                        *capturedOutcome = r.outcome;

                        SessionNotification n;
                        n.sessionId = sid;
                        n.update.sessionUpdate
                            = QString::fromLatin1(SessionUpdateKind::AgentMessageChunk);
                        n.update.content = ContentBlock::makeText(
                            QString("read=%1;perm=%2").arg(*capturedContent, r.outcome));
                        agentSession->sendNotification(
                            QLatin1String(Method::SessionUpdate), n.toJson());

                        PromptResult pr;
                        pr.stopReason = QString::fromLatin1(StopReason::EndTurn);
                        return pr.toJson();
                    });
        });

    serverTransport->start();

    AcpClient client(clientTransport);
    DefaultFileSystemProvider fs;
    CallbackPermissionProvider perm(
        [](const QString &, const ToolCall &, const QList<PermissionOption> &opts) {
            return RequestPermissionResult::selected(opts.first().optionId);
        });
    client.setFileSystemProvider(&fs);
    client.setPermissionProvider(&perm);

    EXPECT_TRUE(client.clientCapabilities().fs.readTextFile);
    EXPECT_TRUE(client.clientCapabilities().fs.writeTextFile);

    waitForFuture(client.connectAndInitialize());
    const NewSessionResult ns = waitForFuture(client.newSession(NewSessionParams{}));

    auto streamed = std::make_shared<QString>();
    QObject::connect(
        &client, &AcpClient::agentMessageChunk,
        [streamed](const QString &, const ContentBlock &c) { streamed->append(c.text); });

    const PromptResult pr
        = waitForFuture(
            client.prompt(ns.sessionId, {ContentBlock::makeText("go")}),
            std::chrono::seconds(8));

    EXPECT_EQ(pr.stopReason, "end_turn");
    EXPECT_EQ(*capturedContent, "hello-from-host");
    EXPECT_EQ(*capturedOutcome, "selected");
    EXPECT_EQ(*streamed, "read=hello-from-host;perm=selected");

    delete agentSession;
    delete serverTransport;
    delete clientTransport;
}


namespace {

QString failureOf(QFuture<PromptResult> future)
{
    if (!future.isFinished()) {
        QEventLoop loop;
        QFutureWatcher<PromptResult> watcher;
        QObject::connect(
            &watcher, &QFutureWatcher<PromptResult>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }

    try {
        future.result();
    } catch (const Rpc::JsonRpcException &e) {
        return e.message();
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }
    return {};
}

} // namespace

TEST_F(AcpLoopbackTest, PromptBeforeInitializeFailsWithoutReachingTheAgent)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    auto *agentSession = new Rpc::JsonRpcSession(serverTransport);
    int requestsSeen = 0;
    QObject::connect(agentSession, &Rpc::JsonRpcSession::incomingRequest, [&requestsSeen]() {
        ++requestsSeen;
    });
    serverTransport->start();

    AcpClient client(clientTransport);
    ASSERT_FALSE(client.isInitialized());

    const QString error = failureOf(client.prompt("s1", {ContentBlock::makeText("hi")}));
    EXPECT_EQ(error, QStringLiteral("Client not initialized"))
        << "a prompt before the handshake must fail the way MCP already fails";
    EXPECT_EQ(requestsSeen, 0) << "nothing may reach the agent before the handshake";

    delete agentSession;
    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, PeerAnswersPingWithoutAnyProtocolHandler)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    auto *agentSession = new Rpc::JsonRpcSession(serverTransport);
    serverTransport->start();

    AcpClient client(clientTransport);
    clientTransport->start();

    const QJsonValue reply
        = waitForFuture(agentSession->sendRequest(QStringLiteral("ping"), QJsonObject{}));
    EXPECT_TRUE(reply.isObject()) << "ping is a JSON-RPC concern, so every peer answers it";

    delete agentSession;
    delete serverTransport;
    delete clientTransport;
}

namespace {

class RecordingTerminalProvider : public AcpTerminalProvider
{
public:
    QFuture<CreateTerminalResult> createTerminal(const CreateTerminalParams &params) override
    {
        created.append(params);
        CreateTerminalResult r;
        r.terminalId = QStringLiteral("term-%1").arg(created.size());
        return LLMQore::readyFuture(r);
    }

    QFuture<TerminalOutputResult> terminalOutput(const QString &, const QString &terminalId) override
    {
        outputAsked.append(terminalId);
        TerminalOutputResult r;
        r.output = QStringLiteral("line-1\n");
        r.truncated = true;
        ExitStatus status;
        status.exitCode = 0;
        r.exitStatus = status;
        return LLMQore::readyFuture(r);
    }

    QFuture<WaitForTerminalExitResult> waitForExit(const QString &, const QString &terminalId) override
    {
        waitedFor.append(terminalId);
        WaitForTerminalExitResult r;
        r.exitCode = 3;
        r.signal = QStringLiteral("none");
        return LLMQore::readyFuture(r);
    }

    QFuture<void> killTerminal(const QString &, const QString &terminalId) override
    {
        killed.append(terminalId);
        return LLMQore::readyFuture();
    }

    QFuture<void> releaseTerminal(const QString &, const QString &terminalId) override
    {
        released.append(terminalId);
        return LLMQore::readyFuture();
    }

    QList<CreateTerminalParams> created;
    QStringList outputAsked;
    QStringList waitedFor;
    QStringList killed;
    QStringList released;
};

} // namespace

TEST_F(AcpLoopbackTest, TerminalCallsReachTheProviderAndComeBackInShape)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    auto *agentSession = new Rpc::JsonRpcSession(serverTransport);
    serverTransport->start();

    AcpClient client(clientTransport);
    RecordingTerminalProvider terminal;
    client.setTerminalProvider(&terminal);
    clientTransport->start();

    CreateTerminalParams create;
    create.sessionId = QStringLiteral("s1");
    create.command = QStringLiteral("ls");
    create.args = QStringList{QStringLiteral("-l")};
    create.cwd = QStringLiteral("/tmp");
    create.env = {EnvVariable{QStringLiteral("K"), QStringLiteral("V")}};
    create.outputByteLimit = 4096;

    const CreateTerminalResult created = CreateTerminalResult::fromJson(
        waitForFuture(
            agentSession->sendRequest(QLatin1String(Method::TerminalCreate), create.toJson()))
            .toObject());
    EXPECT_EQ(created.terminalId, QStringLiteral("term-1"));
    ASSERT_EQ(terminal.created.size(), 1);
    EXPECT_EQ(terminal.created.first().command, QStringLiteral("ls"));
    EXPECT_EQ(terminal.created.first().args, QStringList{QStringLiteral("-l")});
    EXPECT_EQ(terminal.created.first().cwd, QStringLiteral("/tmp"));
    ASSERT_EQ(terminal.created.first().env.size(), 1);
    EXPECT_EQ(terminal.created.first().env.first().name, QStringLiteral("K"));
    ASSERT_TRUE(terminal.created.first().outputByteLimit.has_value());
    EXPECT_EQ(*terminal.created.first().outputByteLimit, 4096);

    TerminalOutputParams outParams;
    outParams.sessionId = QStringLiteral("s1");
    outParams.terminalId = created.terminalId;
    const TerminalOutputResult out = TerminalOutputResult::fromJson(
        waitForFuture(
            agentSession->sendRequest(QLatin1String(Method::TerminalOutput), outParams.toJson()))
            .toObject());
    EXPECT_EQ(out.output, QStringLiteral("line-1\n"));
    EXPECT_TRUE(out.truncated);
    ASSERT_TRUE(out.exitStatus.has_value());
    ASSERT_TRUE(out.exitStatus->exitCode.has_value());
    EXPECT_EQ(*out.exitStatus->exitCode, 0);
    EXPECT_EQ(terminal.outputAsked, QStringList{created.terminalId});

    TerminalRefParams ref;
    ref.sessionId = QStringLiteral("s1");
    ref.terminalId = created.terminalId;

    const WaitForTerminalExitResult exit = WaitForTerminalExitResult::fromJson(
        waitForFuture(
            agentSession->sendRequest(QLatin1String(Method::TerminalWaitForExit), ref.toJson()))
            .toObject());
    ASSERT_TRUE(exit.exitCode.has_value());
    EXPECT_EQ(*exit.exitCode, 3);
    EXPECT_EQ(exit.signal, QStringLiteral("none"));
    EXPECT_EQ(terminal.waitedFor, QStringList{created.terminalId});

    EXPECT_TRUE(waitForFuture(
                    agentSession->sendRequest(QLatin1String(Method::TerminalKill), ref.toJson()))
                    .isObject());
    EXPECT_EQ(terminal.killed, QStringList{created.terminalId});

    EXPECT_TRUE(waitForFuture(
                    agentSession->sendRequest(QLatin1String(Method::TerminalRelease), ref.toJson()))
                    .isObject());
    EXPECT_EQ(terminal.released, QStringList{created.terminalId});

    delete agentSession;
    delete serverTransport;
    delete clientTransport;
}

TEST_F(AcpLoopbackTest, TerminalCallsAreRefusedWhenNoProviderIsWired)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    auto *agentSession = new Rpc::JsonRpcSession(serverTransport);
    serverTransport->start();

    AcpClient client(clientTransport);
    clientTransport->start();

    TerminalRefParams ref;
    ref.sessionId = QStringLiteral("s1");
    ref.terminalId = QStringLiteral("term-1");

    QString error;
    try {
        QFuture<QJsonValue> f
            = agentSession->sendRequest(QLatin1String(Method::TerminalKill), ref.toJson());
        QEventLoop loop;
        QFutureWatcher<QJsonValue> watcher;
        QObject::connect(&watcher, &QFutureWatcher<QJsonValue>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(f);
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        loop.exec();
        f.result();
    } catch (const Rpc::JsonRpcException &e) {
        error = e.message();
    }

    EXPECT_TRUE(error.contains(QStringLiteral("terminal not supported")))
        << "the refusal must name the capability, once: " << qPrintable(error);
    EXPECT_TRUE(error.contains(QString::number(Rpc::ErrorCode::MethodNotFound)))
        << qPrintable(error);

    delete agentSession;
    delete serverTransport;
    delete clientTransport;
}
