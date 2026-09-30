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

QList<QPair<QByteArray, QByteArray>> sessionHeaders(const QByteArray &sessionId)
{
    return {{"Content-Type", "application/json"}, {"Mcp-Session-Id", sessionId}};
}

class QtWarningCapture
{
public:
    QtWarningCapture()
        : m_previous(qInstallMessageHandler(&QtWarningCapture::record))
    {
        s_warnings.clear();
    }
    ~QtWarningCapture() { qInstallMessageHandler(m_previous); }

    QtWarningCapture(const QtWarningCapture &) = delete;
    QtWarningCapture &operator=(const QtWarningCapture &) = delete;

    QStringList warnings() const { return s_warnings; }

private:
    static void record(QtMsgType type, const QMessageLogContext &, const QString &message)
    {
        if (type == QtWarningMsg)
            s_warnings.append(message);
    }

    static inline QStringList s_warnings;
    QtMessageHandler m_previous = nullptr;
};

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

TEST_F(McpHttpServerTest, LegacySpecClosesWhenTheHttpTransportDeletesItsStream)
{
    auto *http = new FakeHttpTransport;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, http);

    QSignalSpy closed(&transport, &Rpc::Transport::closed);

    transport.start();
    ASSERT_TRUE(transport.isOpen());

    delete http;

    EXPECT_FALSE(transport.isOpen())
        << "a stream its HttpTransport destroyed delivers nothing more";
    EXPECT_EQ(closed.size(), 1);
}

TEST_F(McpHttpServerTest, LegacySpecClosesItsStreamWithoutQtWarnings)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    const QtWarningCapture capture;

    transport.start();
    http.lastStream()->sendFinished();
    spin();

    transport.start();
    transport.stop();
    spin();

    EXPECT_TRUE(capture.warnings().isEmpty()) << qPrintable(capture.warnings().join('\n'));
}

TEST_F(McpHttpServerTest, LegacySpecStreamUsesTheSseIdleTimeout)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    cfg.requestTimeoutMs = 7000;
    cfg.sseIdleTimeoutMs = 45000;
    McpSseHttpTransport transport(cfg, &http);

    transport.start();
    ASSERT_EQ(http.streamCount(), 1);
    EXPECT_EQ(http.streamRequest(0).request.transferTimeout(), 45000)
        << "the standing stream is quiet by design and must not inherit the request timeout";
}

TEST_F(McpHttpServerTest, LegacySpecPostsCarryTheRequestTimeout)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    cfg.requestTimeoutMs = 7000;
    McpSseHttpTransport transport(cfg, &http);

    transport.start();
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages\n\n");
    transport.send(jsonRpcRequest(1, "initialize"));

    ASSERT_EQ(http.bufferedCount(), 1);
    EXPECT_EQ(http.bufferedRequest(0).request.transferTimeout(), 7000);
}

TEST_F(McpHttpServerTest, LegacySpecFailsARequestWhosePostFails)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    transport.start();
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages\n\n");
    transport.send(jsonRpcRequest(1, "tools/call"));
    http.failLast("connection reset");
    spin();

    ASSERT_EQ(failed.size(), 1) << "the answer will never come over the stream for this request";
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 1);
    EXPECT_TRUE(failed.first().at(1).toString().contains("connection reset"))
        << qPrintable(failed.first().at(1).toString());
}

TEST_F(McpHttpServerTest, LegacySpecFailsARequestWhosePostIsRejected)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    transport.start();
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages\n\n");
    transport.send(jsonRpcRequest(1, "tools/call"));
    http.respondToLast(500, "session lost", {{"Content-Type", "text/plain"}});
    spin();

    ASSERT_EQ(failed.size(), 1);
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 1);
    EXPECT_TRUE(failed.first().at(1).toString().contains("500"))
        << qPrintable(failed.first().at(1).toString());
}

TEST_F(McpHttpServerTest, LegacySpecDoesNotFailAnAcceptedPost)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    transport.start();
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages\n\n");
    transport.send(jsonRpcRequest(1, "tools/call"));
    http.respondToLast(202, {});
    spin();

    EXPECT_TRUE(failed.isEmpty())
        << "on 2024-11-05 the answer arrives over the stream, not the POST";
}

TEST_F(McpHttpServerTest, LegacySpecReportsAnHttpErrorOnTheStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);
    QSignalSpy closed(&transport, &Rpc::Transport::closed);

    transport.start();
    http.lastStream()->sendHeaders(401, {{"Content-Type", "application/json"}});
    http.lastStream()->sendChunk("{\"error\":\"invalid_token\"}");
    http.lastStream()->sendFinished();
    spin();

    ASSERT_EQ(errors.size(), 1) << "an expired token must not look like a server that went away";
    EXPECT_TRUE(errors.first().first().toString().contains("401"))
        << qPrintable(errors.first().first().toString());
    EXPECT_FALSE(transport.isOpen());
    EXPECT_EQ(closed.size(), 1);
}

TEST_F(McpHttpServerTest, LegacySpecRejectsAStreamThatIsNotAnEventStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

    transport.start();
    http.lastStream()->sendHeaders(200, {{"Content-Type", "text/html"}});
    spin();

    ASSERT_EQ(errors.size(), 1) << "a page that never announces an endpoint must not hang sends";
    EXPECT_TRUE(errors.first().first().toString().contains("text/html"))
        << qPrintable(errors.first().first().toString());
    EXPECT_FALSE(transport.isOpen());
}

TEST_F(McpHttpServerTest, LegacySpecAcceptsAnEventStreamWithParameters)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

    transport.start();
    http.lastStream()->sendHeaders(200, {{"content-type", "Text/Event-Stream; charset=utf-8"}});
    http.lastStream()->sendChunk("event: endpoint\ndata: /messages\n\n");
    transport.send(jsonRpcRequest(1, "initialize"));
    spin();

    EXPECT_TRUE(errors.isEmpty()) << qPrintable(errors.value(0).value(0).toString());
    EXPECT_TRUE(transport.isOpen());
    EXPECT_EQ(http.bufferedCount(), 1);
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

TEST_F(McpHttpServerTest, LatestSpecDropsTheSessionWhenTheServerAnswers404)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy closed(&transport, &Rpc::Transport::closed);

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    http.respondToLast(200, compact(jsonRpcResult(1, "ok")), sessionHeaders("sess-42"));
    spin();

    transport.send(jsonRpcRequest(2, "tools/list"));
    ASSERT_EQ(http.bufferedRequest(1).header("Mcp-Session-Id"), QByteArray("sess-42"));
    http.respondToLast(404, {});
    spin();

    EXPECT_EQ(closed.size(), 1) << "the owner must learn the session is gone to initialize again";
    EXPECT_FALSE(transport.isOpen());
    EXPECT_TRUE(transport.sessionId().isEmpty()) << qPrintable(transport.sessionId());

    transport.start();
    transport.send(jsonRpcRequest(3, "initialize"));
    const auto reinitialize = http.bufferedRequest(http.bufferedCount() - 1);
    EXPECT_EQ(reinitialize.payload().value("id").toInt(), 3);
    EXPECT_FALSE(reinitialize.request.hasRawHeader("Mcp-Session-Id"))
        << "after a 404 the client must start a new session without the old id";
}

TEST_F(McpHttpServerTest, LatestSpecStartsWithoutASessionAfterStop)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    http.respondToLast(200, compact(jsonRpcResult(1, "ok")), sessionHeaders("sess-42"));
    spin();
    ASSERT_EQ(transport.sessionId(), QString("sess-42")) << qPrintable(transport.sessionId());

    transport.stop();
    EXPECT_TRUE(transport.sessionId().isEmpty()) << qPrintable(transport.sessionId());

    transport.start();
    transport.send(jsonRpcRequest(2, "initialize"));
    const auto restarted = http.bufferedRequest(http.bufferedCount() - 1);
    EXPECT_EQ(restarted.payload().value("id").toInt(), 2);
    EXPECT_FALSE(restarted.request.hasRawHeader("Mcp-Session-Id"))
        << "a restarted transport must begin a new session";
}

TEST_F(McpHttpServerTest, LatestSpecIgnoresResponsesToPostsSentBeforeStop)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    transport.send(jsonRpcRequest(1, "tools/call"));
    transport.stop();

    transport.start();
    transport.send(jsonRpcRequest(2, "initialize"));
    http.respondToLast(200, compact(jsonRpcResult(2, "ok")), sessionHeaders("sess-new"));
    spin();

    http.respondTo(0, 200, compact(jsonRpcResult(1, "late")), sessionHeaders("sess-old"));
    spin();

    ASSERT_EQ(messages.size(), 1)
        << "a response to a POST sent before stop() belongs to a dead session";
    EXPECT_EQ(messages.first().first().toJsonObject().value("id").toInt(), 2);
    EXPECT_EQ(transport.sessionId(), QString("sess-new"))
        << "a late response must not replace the current session id, got "
        << qPrintable(transport.sessionId());
}

TEST_F(McpHttpServerTest, LatestSpecIgnoresFailuresOfPostsSentBeforeStop)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

    transport.start();
    transport.send(jsonRpcRequest(1, "tools/call"));
    transport.stop();
    transport.start();

    http.failLast("connection reset");
    spin();

    EXPECT_EQ(errors.size(), 0) << "a POST sent before stop() cannot fail the new session";
}

TEST_F(McpHttpServerTest, LatestSpecKeepsTheSessionAcrossOtherHttpErrors)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy closed(&transport, &Rpc::Transport::closed);

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    http.respondToLast(200, compact(jsonRpcResult(1, "ok")), sessionHeaders("sess-42"));
    spin();

    transport.send(jsonRpcRequest(2, "tools/call"));
    http.respondToLast(503, "upstream down");
    spin();

    EXPECT_EQ(closed.size(), 0);
    EXPECT_TRUE(transport.isOpen());

    transport.send(jsonRpcRequest(3, "tools/list"));
    EXPECT_EQ(
        http.bufferedRequest(http.bufferedCount() - 1).header("Mcp-Session-Id"),
        QByteArray("sess-42"))
        << "only a 404 means the server no longer knows the session";
}

TEST_F(McpHttpServerTest, LatestSpecPostsCarryTheRequestTimeout)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.requestTimeoutMs = 7000;
    McpStreamableHttpTransport transport(cfg, &http);

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));

    ASSERT_EQ(http.bufferedCount(), 1);
    EXPECT_EQ(http.bufferedRequest(0).request.transferTimeout(), 7000);
}

TEST_F(McpHttpServerTest, InjectedHttpTransportKeepsItsOwnTimeout)
{
    FakeHttpTransport http;
    http.setTransferTimeout(45000);

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.requestTimeoutMs = 7000;

    McpStreamableHttpTransport streamable(cfg, &http);
    McpSseHttpTransport sse(cfg, &http);

    EXPECT_EQ(http.transferTimeoutMs(), 45000)
        << "an injected transport belongs to the caller, and so does its timeout";
}

TEST_F(McpHttpServerTest, LatestSpecFailsARequestWhosePostFails)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    transport.start();
    transport.send(jsonRpcRequest(1, "tools/call"));
    http.failLast("connection reset");
    spin();

    ASSERT_EQ(failed.size(), 1) << "the pending request must learn its POST never got through";
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 1);
    EXPECT_TRUE(failed.first().at(1).toString().contains("connection reset"))
        << qPrintable(failed.first().at(1).toString());
}

TEST_F(McpHttpServerTest, LatestSpecFailsARequestThePostDidNotAnswer)
{
    struct Outcome
    {
        const char *name;
        int status;
        QByteArray contentType;
        QByteArray body;
        QString reasonPart;
    };
    const QByteArray progressEvent
        = "event: message\ndata: "
          + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/progress"}})
          + "\n\n";
    const QList<Outcome> outcomes{
        {"http error", 503, "text/plain", "upstream down", "503"},
        {"accepted without a body", 202, "application/json", {}, {}},
        {"broken json", 200, "application/json", "not json", {}},
        {"unexpected content type", 200, "text/plain", "hello", {}},
        {"event stream without the response", 200, "text/event-stream", progressEvent, {}},
    };

    for (const Outcome &outcome : outcomes) {
        SCOPED_TRACE(outcome.name);

        FakeHttpTransport http;

        HttpTransportConfig cfg;
        cfg.endpoint = QUrl("http://mcp.local/mcp");
        McpStreamableHttpTransport transport(cfg, &http);

        QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

        transport.start();
        transport.send(jsonRpcRequest(1, "tools/call"));
        http.respondToLast(outcome.status, outcome.body, {{"Content-Type", outcome.contentType}});
        spin();

        EXPECT_EQ(failed.size(), 1) << "an unanswered request would otherwise wait for its timer";
        if (failed.size() != 1)
            continue;
        EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 1);
        EXPECT_TRUE(failed.first().at(1).toString().contains(outcome.reasonPart))
            << qPrintable(failed.first().at(1).toString());
    }
}

TEST_F(McpHttpServerTest, LatestSpecForwardsAJsonRpcErrorFromAnHttpErrorBody)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);
    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    transport.start();
    transport.send(jsonRpcRequest(1, "tools/list"));
    const QJsonObject serverError{
        {"jsonrpc", "2.0"},
        {"id", QJsonValue(QJsonValue::Null)},
        {"error",
         QJsonObject{{"code", -32000}, {"message", "Bad Request: No valid session ID provided"}}},
    };
    http.respondToLast(400, compact(serverError), {{"Content-Type", "application/json"}});
    spin();

    ASSERT_EQ(messages.size(), 1) << "the server's own error says more than its status";
    const QJsonObject forwarded = messages.first().first().toJsonObject();
    EXPECT_EQ(forwarded.value("id").toInt(), 1) << "the error must reach the request it answers";
    EXPECT_EQ(forwarded.value("error").toObject().value("code").toInt(), -32000);
    EXPECT_TRUE(failed.isEmpty());
}

TEST_F(McpHttpServerTest, LatestSpecDoesNotFailNotificationsOrAnsweredRequests)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    transport.start();
    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    http.respondToLast(202, {});

    transport.send(jsonRpcRequest(2, "ping"));
    http.respondToLast(200, compact(jsonRpcResult(2, "pong")));

    transport.send(jsonRpcRequest(3, "tools/call"));
    const QByteArray sseBody
        = "event: message\ndata: "
          + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/progress"}})
          + "\n\nevent: message\ndata: " + compact(jsonRpcResult(3, "done")) + "\n\n";
    http.respondToLast(200, sseBody, {{"Content-Type", "text/event-stream"}});
    spin();

    EXPECT_TRUE(failed.isEmpty()) << qPrintable(failed.value(0).value(1).toString());
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

