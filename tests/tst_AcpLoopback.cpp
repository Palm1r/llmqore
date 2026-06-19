// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
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

using namespace LLMQore;
using namespace LLMQore::Acp;

namespace {

template<typename T>
T waitForFuture(const QFuture<T> &future, int timeoutMs = 5000)
{
    if (future.isFinished())
        return future.result();
    QEventLoop loop;
    QFutureWatcher<T> watcher;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return future.result();
}

QFuture<QJsonValue> resolvedJson(const QJsonValue &v)
{
    auto p = std::make_shared<QPromise<QJsonValue>>();
    p->start();
    p->addResult(v);
    p->finish();
    return p->future();
}

// Minimal in-process ACP agent built on a JsonRpcSession: answers initialize /
// session/new and streams agent_message_chunk updates during session/prompt.
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
        = waitForFuture(client.prompt(ns.sessionId, {ContentBlock::makeText("go")}), 8000);

    EXPECT_EQ(pr.stopReason, "end_turn");
    EXPECT_EQ(*capturedContent, "hello-from-host");
    EXPECT_EQ(*capturedOutcome, "selected");
    EXPECT_EQ(*streamed, "read=hello-from-host;perm=selected");

    delete agentSession;
    delete serverTransport;
    delete clientTransport;
}
