// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpServer.hpp>
#include <LLMQore/McpToolBinder.hpp>
#include <LLMQore/RpcPipeTransport.hpp>
#include <LLMQore/ToolRegistry.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "clients/openai/OpenAIMessage.hpp"

#include "TestHelpers.hpp"

using namespace LLMQore;
using namespace LLMQore::Mcp;

namespace {

class CannedTool : public BaseTool
{
    Q_OBJECT
public:
    CannedTool(QString id, QString reply, QObject *parent = nullptr)
        : BaseTool(parent)
        , m_id(std::move(id))
        , m_reply(std::move(reply))
    {}
    QString id() const override { return m_id; }
    QString displayName() const override { return m_id; }
    QString description() const override { return QString("Replies with %1").arg(m_reply); }
    QJsonObject parametersSchema() const override
    {
        return QJsonObject{{"type", "object"}};
    }
    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &) override
    {
        const QString reply = m_reply;
        return QtConcurrent::run(
            [reply]() -> LLMQore::ToolResult { return LLMQore::ToolResult::text(reply); });
    }

private:
    QString m_id;
    QString m_reply;
};

struct Loopback
{
    Rpc::PipeTransport *serverTransport = nullptr;
    Rpc::PipeTransport *clientTransport = nullptr;
    McpServer *server = nullptr;
    McpClient *client = nullptr;

    explicit Loopback(const QString &serverName)
    {
        auto [st, ct] = Rpc::PipeTransport::createPair();
        serverTransport = st;
        clientTransport = ct;
        server = new McpServer(serverTransport, McpServerConfig{{serverName, "0.0.1"}});
        client = new McpClient(clientTransport);
        server->start();
    }

    ~Loopback()
    {
        delete client;
        delete server;
        delete serverTransport;
        delete clientTransport;
    }

    void initialize() { waitForFuture(client->connectAndInitialize()); }
};

struct ToollessUpstream
{
    Rpc::PipeTransport *serverTransport = nullptr;
    Rpc::PipeTransport *clientTransport = nullptr;
    McpClient *client = nullptr;
    int toolListings = 0;

    ToollessUpstream()
    {
        auto [st, ct] = Rpc::PipeTransport::createPair();
        serverTransport = st;
        clientTransport = ct;
        QObject::connect(
            serverTransport,
            &Rpc::Transport::messageReceived,
            serverTransport,
            [this](const QJsonObject &message) { answer(message); });
        serverTransport->start();
        client = new McpClient(clientTransport);
    }
    ~ToollessUpstream()
    {
        delete client;
        delete serverTransport;
        delete clientTransport;
    }

    void answer(const QJsonObject &message)
    {
        if (!message.contains("id"))
            return;
        const QString method = message.value("method").toString();
        QJsonObject reply{{"jsonrpc", "2.0"}, {"id", message.value("id")}};
        if (method == "initialize") {
            reply.insert(
                "result",
                QJsonObject{
                    {"protocolVersion", "2025-06-18"},
                    {"capabilities", QJsonObject{}},
                    {"serverInfo", QJsonObject{{"name", "quiet"}, {"version", "0.0.1"}}},
                });
        } else {
            if (method == "tools/list")
                ++toolListings;
            reply.insert(
                "error", QJsonObject{{"code", -32601}, {"message", "Method not found"}});
        }
        serverTransport->send(reply);
    }

    void initialize() { waitForFuture(client->connectAndInitialize()); }
};

class McpToolBinderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_McpToolBinder";
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

TEST_F(McpToolBinderTest, PrefixesToolIdsWithServerName)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(loop.client, "alpha");
    ASSERT_TRUE(waitForSignal(synced, 1));

    ASSERT_EQ(registry.registeredTools().size(), 1);
    BaseTool *tool = registry.tool("alpha_echo");
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->id(), "alpha_echo");
    EXPECT_EQ(registry.tool("echo"), nullptr);
}

TEST_F(McpToolBinderTest, TwoUpstreamsWithSameToolNameDoNotCollide)
{
    Loopback a("upstream-a");
    Loopback b("upstream-b");
    a.server->addTool(new CannedTool("read_file", "from-a", a.server));
    b.server->addTool(new CannedTool("read_file", "from-b", b.server));
    a.initialize();
    b.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(a.client, "alpha");
    binder.addClient(b.client, "beta");
    ASSERT_TRUE(waitForSignal(synced, 2));

    ASSERT_EQ(registry.registeredTools().size(), 2);
    BaseTool *toolA = registry.tool("alpha_read_file");
    BaseTool *toolB = registry.tool("beta_read_file");
    ASSERT_NE(toolA, nullptr);
    ASSERT_NE(toolB, nullptr);

    EXPECT_EQ(waitForFuture(toolA->executeAsync(QJsonObject{})).asText(), "from-a");
    EXPECT_EQ(waitForFuture(toolB->executeAsync(QJsonObject{})).asText(), "from-b");
}

TEST_F(McpToolBinderTest, DiffResyncFollowsToolsChanged)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(loop.client, "srv");
    ASSERT_TRUE(waitForSignal(synced, 1));
    EXPECT_EQ(registry.registeredTools().size(), 1);

    loop.server->addTool(new CannedTool("upper", "HI", loop.server));
    ASSERT_TRUE(waitForSignal(synced, 2));
    EXPECT_NE(registry.tool("srv_echo"), nullptr);
    EXPECT_NE(registry.tool("srv_upper"), nullptr);

    loop.server->removeTool("upper");
    ASSERT_TRUE(waitForSignal(synced, 3));
    EXPECT_NE(registry.tool("srv_echo"), nullptr);
    EXPECT_EQ(registry.tool("srv_upper"), nullptr);
    EXPECT_EQ(registry.registeredTools().size(), 1);
}

TEST_F(McpToolBinderTest, RemovesToolsWhenClientDestroyed)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(loop.client, "srv");
    ASSERT_TRUE(waitForSignal(synced, 1));
    EXPECT_EQ(registry.registeredTools().size(), 1);

    delete loop.client;
    loop.client = nullptr;
    pumpEventLoop(std::chrono::milliseconds(50));

    EXPECT_EQ(registry.registeredTools().size(), 0);
}

TEST_F(McpToolBinderTest, RemoveClientDetachesWithoutTouchingClient)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(loop.client, "srv");
    ASSERT_TRUE(waitForSignal(synced, 1));

    binder.removeClient(loop.client);
    pumpEventLoop(std::chrono::milliseconds(50));
    EXPECT_EQ(registry.registeredTools().size(), 0);
    EXPECT_TRUE(loop.client->isInitialized());
}

TEST_F(McpToolBinderTest, ReconnectsAfterTransportLoss)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);
    QSignalSpy dropped(&binder, &McpToolBinder::serverDisconnected);

    binder.addClient(loop.client, "srv", /*autoReconnect*/ true);
    ASSERT_TRUE(waitForSignal(synced, 1));
    EXPECT_EQ(registry.registeredTools().size(), 1);

    loop.clientTransport->stop();
    ASSERT_TRUE(waitForSignal(dropped, 1));
    EXPECT_EQ(registry.registeredTools().size(), 0);

    ASSERT_TRUE(waitForSignal(synced, 2, std::chrono::seconds(10)));
    EXPECT_NE(registry.tool("srv_echo"), nullptr);
    EXPECT_EQ(waitForFuture(registry.tool("srv_echo")->executeAsync(QJsonObject{})).asText(), "hi");
}

TEST_F(McpToolBinderTest, BindsAServerWithoutToolsWithoutListingThem)
{
    ToollessUpstream upstream;
    upstream.initialize();

    ToolRegistry registry;
    McpToolBinder binder(&registry);
    QSignalSpy synced(&binder, &McpToolBinder::toolsSynced);

    binder.addClient(upstream.client, "quiet", /*autoReconnect*/ true);
    ASSERT_TRUE(waitForSignal(synced, 1));
    EXPECT_EQ(synced.first().at(1).toInt(), 0);
    EXPECT_EQ(upstream.toolListings, 0) << "a server that offers no tools has none to list";

    upstream.clientTransport->stop();
    ASSERT_TRUE(waitForSignal(synced, 2, std::chrono::seconds(10)));
    EXPECT_EQ(upstream.toolListings, 0)
        << "a failed listing would count as a failed initialize and be retried forever";
}

TEST_F(McpToolBinderTest, ToolsManagerRelaysToolsSyncedAndTheServerName)
{
    Loopback loop("upstream");
    loop.server->addTool(new CannedTool("echo", "hi", loop.server));
    loop.initialize();

    ToolsManager tools(OpenAIMessage::toolDialect());
    QSignalSpy synced(&tools, &ToolsManager::mcpToolsSynced);

    tools.addMcpClient(loop.client, "alpha");
    ASSERT_TRUE(waitForSignal(synced, 1)) << "the facade never re-emitted the binder's signal";

    EXPECT_EQ(synced.first().at(0).toString(), QStringLiteral("alpha"));
    EXPECT_EQ(synced.first().at(1).toInt(), 1);
    EXPECT_NE(tools.tool("alpha_echo"), nullptr)
        << "the facade dropped the server name, so the tool went in unprefixed";
}

TEST_F(McpToolBinderTest, ToolsManagerReportsTheFateOfAnMcpServer)
{
    ToolsManager tools(OpenAIMessage::toolDialect());
    QSignalSpy failed(&tools, &ToolsManager::mcpServerInitFailed);

    ServerEndpoint nothing;
    nothing.name = QStringLiteral("nothing");
    EXPECT_FALSE(tools.addMcpServer(nothing)) << "neither an http url nor a command";

    ServerEndpoint broken;
    broken.name = QStringLiteral("broken");
    broken.command = QStringLiteral("llmqore-no-such-mcp-server");

    EXPECT_TRUE(tools.addMcpServer(broken)) << "a transport was built, so the attempt was made";
    ASSERT_TRUE(waitForSignal(failed, 1))
        << "a consumer of ToolsManager cannot learn that the server never came up";
    EXPECT_EQ(failed.first().at(0).toString(), QStringLiteral("broken"));

    tools.shutdownMcp();
    pumpEventLoop(std::chrono::milliseconds(1500));
    EXPECT_EQ(failed.size(), 1) << "shutdown must stop the reconnect ladder";
}

TEST_F(McpToolBinderTest, ToolsManagerReportsHowManyServersWereLoaded)
{
    ToolsManager tools(OpenAIMessage::toolDialect());

    const QJsonObject config{
        {"mcpServers",
         QJsonObject{
             {"alpha", QJsonObject{{"command", "llmqore-no-such-mcp-server"}}},
             {"beta", QJsonObject{{"enable", false}, {"command", "anything"}}},
             {"gamma", QJsonObject{}},
         }},
    };

    EXPECT_EQ(tools.loadMcpServers(config), 1)
        << "a host cannot tell an empty config from a fully unusable one";

    tools.shutdownMcp();
}

#include "tst_McpToolBinder.moc"
