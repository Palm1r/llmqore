// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <QSignalSpy>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpHttpServerTransport.hpp>
#include <LLMQore/McpHttpTransport.hpp>
#include <LLMQore/McpServer.hpp>

#include "FakeHttpTransport.hpp"

#include "TestHelpers.hpp"

using namespace LLMQore;
using namespace LLMQore::Mcp;

using LLMQoreTest::FakeHttpStream;
using LLMQoreTest::FakeHttpTransport;

namespace {

void spin(int rounds = 8)
{
    for (int i = 0; i < rounds; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

QJsonObject jsonRpcResult(int id, const QString &value)
{
    return QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", QJsonObject{{"value", value}}},
    };
}

QByteArray compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject jsonRpcRequest(int id, const QString &method)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}};
}

// Minimal tool so we have something for the HTTP loopback to exercise.
class EchoTool : public BaseTool
{
    Q_OBJECT
public:
    explicit EchoTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}
    QString id() const override { return "echo"; }
    QString displayName() const override { return "Echo"; }
    QString description() const override { return "Echoes its input text"; }
    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{{"text", QJsonObject{{"type", "string"}}}}},
            {"required", QJsonArray{"text"}},
        };
    }
    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        const QString text = input.value("text").toString();
        return QtConcurrent::run([text]() -> LLMQore::ToolResult {
            return LLMQore::ToolResult::text(QString("echo: %1").arg(text));
        });
    }
};

class McpHttpServerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_McpHttpServer";
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

// End-to-end: spin up McpHttpServerTransport on a random port, point
// McpHttpTransport (2025-03-26 spec) at it, run a real handshake + tool
// call over the loop. Proves that the server transport correctly:
//   - parses HTTP/1.1 POSTs with Content-Length
//   - routes JSON-RPC requests into the session
//   - matches outgoing responses to the socket that originated the request
//   - sets Mcp-Session-Id on every response
TEST_F(McpHttpServerTest, HandshakeAndToolCallOverHttp)
{
    HttpServerConfig serverCfg;
    serverCfg.address = QHostAddress::LocalHost;
    serverCfg.port = 0; // OS-assigned
    serverCfg.path = "/mcp";
    auto *serverTransport = new McpHttpServerTransport(serverCfg);

    McpServerConfig scfg;
    scfg.serverInfo = {"http-loopback-server", "0.0.1"};
    McpServer server(serverTransport, scfg);
    server.addTool(new EchoTool(&server));

    server.start();
    ASSERT_TRUE(serverTransport->isOpen());
    const quint16 port = serverTransport->serverPort();
    ASSERT_GT(port, 0u);

    HttpTransportConfig clientCfg;
    clientCfg.endpoint = QUrl(QString("http://127.0.0.1:%1/mcp").arg(port));
    clientCfg.spec = McpHttpSpec::V2025_03_26;
    auto *clientTransport = new McpStreamableHttpTransport(clientCfg);
    McpClient client(clientTransport, Implementation{"http-loopback-client", "0.0.1"});

    const InitializeResult init = waitForFuture(
        client.connectAndInitialize(std::chrono::seconds(5)));
    EXPECT_EQ(init.serverInfo.name, "http-loopback-server");
    EXPECT_TRUE(client.isInitialized());

    const QList<ToolInfo> tools = waitForFuture(client.listTools());
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools.first().name, "echo");

    const LLMQore::ToolResult result = waitForFuture(
        client.callTool("echo", QJsonObject{{"text", "over-http"}}));
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.asText(), "echo: over-http");

    client.shutdown();
    server.stop();

    delete clientTransport;
    delete serverTransport;
}

TEST_F(McpHttpServerTest, LatestSpecPostsStraightToTheConfiguredEndpoint)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.spec = McpHttpSpec::V2025_03_26;
    McpStreamableHttpTransport transport(cfg, &http);

    transport.start();
    EXPECT_TRUE(transport.isOpen());
    EXPECT_EQ(http.streamCount(), 0) << "2025-03-26 must not open a standing SSE stream";

    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}});
    ASSERT_EQ(http.bufferedCount(), 1);

    const auto sent = http.bufferedRequest(0);
    EXPECT_EQ(sent.verb, QByteArray("POST"));
    EXPECT_EQ(sent.url(), cfg.endpoint);
    EXPECT_EQ(sent.header("Accept"), QByteArray("application/json, text/event-stream"));
    EXPECT_EQ(sent.payload().value("method").toString(), "ping");
}

TEST_F(McpHttpServerTest, LegacySpecOpensSseStreamAndWaitsForTheEndpointEvent)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    cfg.spec = McpHttpSpec::V2024_11_05;
    cfg.headers.insert("X-Tenant", "acme");
    McpSseHttpTransport transport(cfg, &http);

    transport.start();
    ASSERT_EQ(http.streamCount(), 1) << "2024-11-05 must open a GET SSE stream";

    const auto streamReq = http.streamRequest(0);
    EXPECT_EQ(streamReq.verb, QByteArray("GET"));
    EXPECT_EQ(streamReq.url(), cfg.endpoint);
    EXPECT_EQ(streamReq.header("Accept"), QByteArray("text/event-stream"));
    EXPECT_EQ(streamReq.header("X-Tenant"), QByteArray("acme"));

    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}});
    EXPECT_EQ(http.bufferedCount(), 0) << "sends before the endpoint event must queue";

    http.lastStream()->sendChunk("event: endpoint\ndata: /messages?sessionId=abc\n\n");
    spin();

    ASSERT_EQ(http.bufferedCount(), 1) << "queued send must flush once the endpoint is known";

    const auto posted = http.bufferedRequest(0);
    EXPECT_EQ(posted.verb, QByteArray("POST"));
    EXPECT_EQ(posted.url(), QUrl("http://mcp.local/messages?sessionId=abc"));
    EXPECT_EQ(posted.header("X-Tenant"), QByteArray("acme"));
}

TEST_F(McpHttpServerTest, LegacySpecDeliversServerMessagesOverTheSseStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    cfg.spec = McpHttpSpec::V2024_11_05;
    McpSseHttpTransport transport(cfg, &http);

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    ASSERT_EQ(http.streamCount(), 1);

    http.lastStream()->sendChunk("event: endpoint\ndata: /messages\n\n");
    http.lastStream()->sendChunk("event: message\ndata: " + compact(jsonRpcResult(7, "pong")) + "\n\n");
    spin();

    ASSERT_EQ(messages.size(), 1);
    const QJsonObject received = messages.first().first().toJsonObject();
    EXPECT_EQ(received.value("id").toInt(), 7);
    EXPECT_EQ(received.value("result").toObject().value("value").toString(), "pong");
}

TEST_F(McpHttpServerTest, LegacySpecDoesNotReuseTheEndpointOfAClosedStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    transport.start();
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages?sessionId=old\n\n");
    http.lastStream()->sendFinished();
    spin();
    ASSERT_FALSE(transport.isOpen());

    transport.start();
    ASSERT_EQ(http.streamCount(), 2);

    transport.send(jsonRpcRequest(1, "initialize"));
    EXPECT_EQ(http.bufferedCount(), 0)
        << "a send on the new stream must wait for its endpoint, not go to the closed session";

    http.lastStream()->sendChunk("event: endpoint\ndata: /messages?sessionId=new\n\n");
    spin();

    ASSERT_EQ(http.bufferedCount(), 1);
    const QUrl posted = http.bufferedRequest(0).url();
    EXPECT_EQ(posted, QUrl("http://mcp.local/messages?sessionId=new"))
        << qPrintable(posted.toString());
}

TEST_F(McpHttpServerTest, LegacySpecFindsTheNewEndpointAfterAStreamCutMidEvent)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    transport.start();
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages?sessionId=old\n\n");
    http.lastStream()->sendChunk("event: message\ndata: {\"jsonrpc\":\"2.0\",");
    http.lastStream()->sendFinished();
    spin();

    transport.start();
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages?sessionId=new\n\n");
    spin();

    transport.send(jsonRpcRequest(1, "initialize"));
    ASSERT_EQ(http.bufferedCount(), 1)
        << "the half-received event of the closed stream must not swallow the new endpoint";
    const QUrl posted = http.bufferedRequest(0).url();
    EXPECT_EQ(posted, QUrl("http://mcp.local/messages?sessionId=new"))
        << qPrintable(posted.toString());
}

TEST_F(McpHttpServerTest, LegacySpecDoesNotReplaySendsQueuedForAClosedStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    http.lastStream()->sendFinished();
    spin();

    transport.start();
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages?sessionId=new\n\n");
    spin();
    EXPECT_EQ(http.bufferedCount(), 0)
        << "a message queued for the closed stream belongs to a session that no longer exists";

    transport.send(jsonRpcRequest(2, "initialize"));
    spin();
    ASSERT_EQ(http.bufferedCount(), 1);
    EXPECT_EQ(http.bufferedRequest(0).payload().value("id").toInt(), 2);
}

TEST_F(McpHttpServerTest, LegacySpecRejectsAnEndpointOutsideTheConnectionOrigin)
{
    const QList<QByteArray> foreignEndpoints{
        "https://attacker.example/collect",
        "//attacker.example/collect",
        "http://mcp.example.com/messages",
        "https://mcp.example.com:8443/messages",
    };

    for (const QByteArray &endpoint : foreignEndpoints) {
        SCOPED_TRACE(endpoint.constData());

        FakeHttpTransport http;

        HttpTransportConfig cfg;
        cfg.endpoint = QUrl("https://mcp.example.com/sse");
        cfg.headers.insert("Authorization", "Bearer secret");
        McpSseHttpTransport transport(cfg, &http);

        QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

        transport.start();
        http.lastStream()->sendChunk("event: endpoint\ndata: " + endpoint + "\n\n");
        spin();

        EXPECT_FALSE(errors.isEmpty()) << "an endpoint on another origin must be reported";
        EXPECT_FALSE(transport.isOpen()) << "the stream announced no endpoint the client may use";

        transport.send(jsonRpcRequest(1, "initialize"));
        spin();
        EXPECT_EQ(http.bufferedCount(), 0)
            << "no message, and no Authorization header, may leave for another origin";
    }
}

TEST_F(McpHttpServerTest, LegacySpecAcceptsAnAbsoluteEndpointOnTheConnectionOrigin)
{
    const QList<QByteArray> sameOriginEndpoints{
        "https://mcp.example.com/messages?sessionId=abc",
        "https://mcp.example.com:443/messages?sessionId=abc",
    };

    for (const QByteArray &endpoint : sameOriginEndpoints) {
        SCOPED_TRACE(endpoint.constData());

        FakeHttpTransport http;

        HttpTransportConfig cfg;
        cfg.endpoint = QUrl("https://mcp.example.com/sse");
        cfg.headers.insert("Authorization", "Bearer secret");
        McpSseHttpTransport transport(cfg, &http);

        transport.start();
        http.lastStream()->sendChunk("event: endpoint\ndata: " + endpoint + "\n\n");
        spin();
        EXPECT_TRUE(transport.isOpen());

        transport.send(jsonRpcRequest(1, "initialize"));
        spin();
        ASSERT_EQ(http.bufferedCount(), 1);
        EXPECT_EQ(http.bufferedRequest(0).url().path(), QString("/messages"));
        EXPECT_EQ(http.bufferedRequest(0).header("Authorization"), QByteArray("Bearer secret"));
    }
}

TEST_F(McpHttpServerTest, SessionIdFromTheFirstResponseIsEchoedOnLaterPosts)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.spec = McpHttpSpec::V2025_03_26;
    McpStreamableHttpTransport transport(cfg, &http);

    transport.start();
    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}});
    ASSERT_EQ(http.bufferedCount(), 1);
    EXPECT_TRUE(http.bufferedRequest(0).header("Mcp-Session-Id").isEmpty());

    http.respondToLast(
        200,
        compact(jsonRpcResult(1, "ok")),
        {{"Content-Type", "application/json"}, {"Mcp-Session-Id", "sess-42"}});
    spin();

    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    ASSERT_EQ(http.bufferedCount(), 2);
    EXPECT_EQ(http.bufferedRequest(1).header("Mcp-Session-Id"), QByteArray("sess-42"));
}

TEST_F(McpHttpServerTest, JsonResponseBodyBecomesOneReceivedMessage)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.spec = McpHttpSpec::V2025_03_26;
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "ping"}});
    http.respondToLast(200, compact(jsonRpcResult(3, "pong")));
    spin();

    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().first().toJsonObject().value("id").toInt(), 3);
}

TEST_F(McpHttpServerTest, EventStreamResponseBodyYieldsEveryFramedMessage)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.spec = McpHttpSpec::V2025_03_26;
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"}});

    const QByteArray sseBody = "event: message\ndata: " + compact(jsonRpcResult(4, "first"))
        + "\n\nevent: message\ndata: " + compact(jsonRpcResult(5, "second")) + "\n\n";
    http.respondToLast(200, sseBody, {{"Content-Type", "text/event-stream"}});
    spin();

    ASSERT_EQ(messages.size(), 2);
    EXPECT_EQ(messages.at(0).first().toJsonObject().value("id").toInt(), 4);
    EXPECT_EQ(messages.at(1).first().toJsonObject().value("id").toInt(), 5);
}

TEST_F(McpHttpServerTest, AcceptedWithoutBodyProducesNoMessageAndNoError)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.spec = McpHttpSpec::V2025_03_26;
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);
    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

    transport.start();
    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    http.respondToLast(202, {});
    spin();

    EXPECT_EQ(messages.size(), 0);
    EXPECT_EQ(errors.size(), 0);
}

TEST_F(McpHttpServerTest, HttpErrorStatusIsReportedAsTransportError)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.spec = McpHttpSpec::V2025_03_26;
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

    transport.start();
    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 9}, {"method", "ping"}});
    http.respondToLast(503, "upstream down");
    spin();

    ASSERT_EQ(errors.size(), 1);
    EXPECT_TRUE(errors.first().first().toString().contains("503"));
}

#include "tst_McpHttpServer.moc"

