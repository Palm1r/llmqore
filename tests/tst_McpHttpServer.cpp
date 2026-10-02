// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <QSignalSpy>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/HttpClient.hpp>
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

QJsonObject initializeReply(int id)
{
    return QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", QJsonObject{{"protocolVersion", "2025-06-18"}}},
    };
}

QList<QPair<QByteArray, QByteArray>> eventStreamHeaders()
{
    return {{"Content-Type", "text/event-stream"}};
}

QByteArray listenEvent(const QString &method, const QByteArray &id = {})
{
    const QByteArray data = compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", method}});
    return (id.isEmpty() ? QByteArray() : "id: " + id + "\n") + "data: " + data + "\n\n";
}

bool openSession(McpStreamableHttpTransport &transport, FakeHttpTransport &http)
{
    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    http.respondToLastStream(200, compact(initializeReply(1)), sessionHeaders("sess-42"));
    spin();
    return http.streamCount() == 2 && http.streamRequest(1).verb == "GET";
}

QTimer *listenRetryTimer(const QObject &transport)
{
    return transport.findChild<QTimer *>(
        QStringLiteral("listenRetryTimer"), Qt::FindDirectChildrenOnly);
}

void fireListenRetry(const QObject &transport)
{
    QTimer *timer = listenRetryTimer(transport);
    if (timer && timer->isActive())
        timer->start(0);
    spin();
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
    EXPECT_EQ(http.streamCount(), 0) << "2025-03-26 opens nothing before a session exists";

    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}});
    ASSERT_EQ(http.streamCount(), 1);

    const auto sent = http.streamRequest(0);
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

TEST_F(McpHttpServerTest, LegacySpecReportsAStreamTheHttpTransportOpensNoStreamFor)
{
    FakeHttpTransport http;
    http.refuseStreams("GET");

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, &http);

    QtWarningCapture capture;
    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);
    transport.start();

    EXPECT_FALSE(transport.isOpen()) << "an open transport would queue sends forever";
    EXPECT_EQ(errors.size(), 1);
    EXPECT_TRUE(capture.warnings().filter("nullptr").isEmpty())
        << capture.warnings().join('\n').toStdString();
}

TEST_F(McpHttpServerTest, LegacySpecStaysClosedOnceTheHttpTransportIsDeleted)
{
    auto *http = new FakeHttpTransport;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, http);
    transport.start();
    ASSERT_TRUE(transport.isOpen());

    delete http;
    ASSERT_FALSE(transport.isOpen());

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);
    transport.start();

    EXPECT_FALSE(transport.isOpen());
    EXPECT_EQ(errors.size(), 1) << "starting again must not reach a deleted HttpTransport";
}

TEST_F(McpHttpServerTest, LegacySpecFailsASendOnceTheHttpTransportIsDeleted)
{
    auto *http = new FakeHttpTransport;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    McpSseHttpTransport transport(cfg, http);
    transport.start();
    http->lastStream()->sendHeaders(200, {{"Content-Type", "text/event-stream"}});
    http->lastStream()->sendChunk("event: endpoint\ndata: /messages\n\n");

    const std::unique_ptr<FakeHttpStream> stream(http->lastStream());
    stream->setParent(nullptr);
    delete http;
    ASSERT_TRUE(transport.isOpen()) << "a custom HttpTransport may not own its streams";

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);
    transport.send(jsonRpcRequest(1, "tools/call"));

    ASSERT_EQ(failed.size(), 1) << "the send must not reach the deleted HttpTransport";
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 1);
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
    ASSERT_EQ(http.streamCount(), 1);
    EXPECT_TRUE(http.streamRequest(0).header("Mcp-Session-Id").isEmpty());

    http.respondToLastStream(
        200,
        compact(jsonRpcResult(1, "ok")),
        {{"Content-Type", "application/json"}, {"Mcp-Session-Id", "sess-42"}});
    spin();

    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    ASSERT_EQ(http.streamCount(), 2);
    EXPECT_EQ(http.streamRequest(1).header("Mcp-Session-Id"), QByteArray("sess-42"));
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
    http.respondToLastStream(200, compact(jsonRpcResult(1, "ok")), sessionHeaders("sess-42"));
    spin();

    transport.send(jsonRpcRequest(2, "tools/list"));
    ASSERT_EQ(http.streamRequest(1).header("Mcp-Session-Id"), QByteArray("sess-42"));
    http.respondToLastStream(404, {});
    spin();

    EXPECT_EQ(closed.size(), 1) << "the owner must learn the session is gone to initialize again";
    EXPECT_FALSE(transport.isOpen());
    EXPECT_TRUE(transport.sessionId().isEmpty()) << qPrintable(transport.sessionId());

    transport.start();
    transport.send(jsonRpcRequest(3, "initialize"));
    const auto reinitialize = http.streamRequest(http.streamCount() - 1);
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
    http.respondToLastStream(200, compact(jsonRpcResult(1, "ok")), sessionHeaders("sess-42"));
    spin();
    ASSERT_EQ(transport.sessionId(), QString("sess-42")) << qPrintable(transport.sessionId());

    transport.stop();
    EXPECT_TRUE(transport.sessionId().isEmpty()) << qPrintable(transport.sessionId());

    transport.start();
    transport.send(jsonRpcRequest(2, "initialize"));
    const auto restarted = http.streamRequest(http.streamCount() - 1);
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
    const QPointer<FakeHttpStream> beforeStop = http.lastStream();
    transport.stop();

    transport.start();
    transport.send(jsonRpcRequest(2, "initialize"));
    http.respondToLastStream(200, compact(jsonRpcResult(2, "ok")), sessionHeaders("sess-new"));

    ASSERT_TRUE(beforeStop) << "the stream of the old POST must still be able to answer late";
    beforeStop->sendHeaders(200, sessionHeaders("sess-old"));
    beforeStop->sendChunk(compact(jsonRpcResult(1, "late")));
    beforeStop->sendFinished();
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

    http.failLastStream("connection reset");
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
    http.respondToLastStream(200, compact(jsonRpcResult(1, "ok")), sessionHeaders("sess-42"));
    spin();

    transport.send(jsonRpcRequest(2, "tools/call"));
    http.respondToLastStream(503, "upstream down");
    spin();

    EXPECT_EQ(closed.size(), 0);
    EXPECT_TRUE(transport.isOpen());

    transport.send(jsonRpcRequest(3, "tools/list"));
    EXPECT_EQ(
        http.streamRequest(http.streamCount() - 1).header("Mcp-Session-Id"),
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

    ASSERT_EQ(http.streamCount(), 1);
    EXPECT_EQ(http.streamRequest(0).request.transferTimeout(), 7000);
}

TEST_F(McpHttpServerTest, LatestSpecDeliversEventsBeforeTheResponseStreamEnds)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);

    transport.start();
    transport.send(jsonRpcRequest(1, "tools/call"));
    ASSERT_EQ(http.streamCount(), 1) << "a POST must be read as it arrives, not once it ends";

    FakeHttpStream *stream = http.lastStream();
    stream->sendHeaders(200, {{"Content-Type", "text/event-stream"}});
    stream->sendChunk(
        "event: message\ndata: "
        + compact(QJsonObject{
            {"jsonrpc", "2.0"},
            {"id", "srv-1"},
            {"method", "elicitation/create"},
            {"params", QJsonObject{{"message", "Name?"}}}})
        + "\n\n");
    spin();

    ASSERT_EQ(messages.size(), 1)
        << "the server waits for this answer before it finishes the stream";
    EXPECT_EQ(
        messages.first().first().toJsonObject().value("method").toString(), "elicitation/create");

    stream->sendChunk("event: message\ndata: " + compact(jsonRpcResult(1, "done")) + "\n\n");
    stream->sendFinished();
    spin();

    EXPECT_EQ(messages.size(), 2);
}

TEST_F(McpHttpServerTest, LatestSpecSendsTheNegotiatedProtocolVersion)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    EXPECT_FALSE(http.streamRequest(0).request.hasRawHeader("MCP-Protocol-Version"))
        << "nothing is negotiated before initialize returns";

    http.respondToLastStream(200, compact(initializeReply(1)));
    spin();

    transport.send(jsonRpcRequest(2, "tools/list"));
    EXPECT_EQ(
        http.lastStreamRequest("POST").header("MCP-Protocol-Version"), QByteArray("2025-06-18"))
        << "every request after initialize must name the negotiated revision";

    transport.stop();
    transport.start();
    transport.send(jsonRpcRequest(3, "initialize"));
    EXPECT_FALSE(http.lastStreamRequest("POST").request.hasRawHeader("MCP-Protocol-Version"))
        << "a new session negotiates again";
}

TEST_F(McpHttpServerTest, LatestSpecOpensAListenStreamOnceInitialized)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.sseIdleTimeoutMs = 45000;
    McpStreamableHttpTransport transport(cfg, &http);

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    EXPECT_EQ(http.streamCount(), 1) << "there is nothing to listen to before a session exists";

    http.respondToLastStream(200, compact(initializeReply(1)), sessionHeaders("sess-42"));
    spin();

    ASSERT_EQ(http.streamCount(), 2)
        << "the server needs a channel for requests it starts on its own";
    const auto listen = http.streamRequest(1);
    EXPECT_EQ(listen.verb, QByteArray("GET"));
    EXPECT_EQ(listen.url(), cfg.endpoint);
    EXPECT_EQ(listen.header("Accept"), QByteArray("text/event-stream"));
    EXPECT_EQ(listen.header("Mcp-Session-Id"), QByteArray("sess-42"));
    EXPECT_EQ(listen.header("MCP-Protocol-Version"), QByteArray("2025-06-18"));
    EXPECT_EQ(listen.request.transferTimeout(), 45000)
        << "the listen stream is quiet by design, like the legacy SSE stream";
}

TEST_F(McpHttpServerTest, LatestSpecDeliversServerRequestsFromTheListenStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());
    listen->sendChunk(
        "event: message\ndata: "
        + compact(QJsonObject{{"jsonrpc", "2.0"}, {"id", "srv-1"}, {"method", "roots/list"}})
        + "\n\n");
    spin();

    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().first().toJsonObject().value("method").toString(), "roots/list");
}

TEST_F(McpHttpServerTest, LatestSpecAcceptsThatAServerOffersNoListenStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

    http.respondToLastStream(405, "Method Not Allowed", {{"Content-Type", "text/plain"}});
    spin();

    ASSERT_TRUE(listenRetryTimer(transport));
    EXPECT_FALSE(listenRetryTimer(transport)->isActive())
        << "405 says the server has no such stream; asking again is noise";
    EXPECT_TRUE(errors.isEmpty()) << "a server without a listen stream is not an error";
    EXPECT_TRUE(transport.isOpen());
}

TEST_F(McpHttpServerTest, LatestSpecReconnectsTheListenStreamFromTheLastEvent)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());
    listen->sendChunk(
        "id: 7\nevent: message\ndata: "
        + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/message"}})
        + "\n\n");
    listen->sendFinished();

    QTimer *retry = listenRetryTimer(transport);
    ASSERT_TRUE(retry);
    ASSERT_TRUE(retry->isActive()) << "a dropped listen stream must come back";
    EXPECT_EQ(retry->interval(), 1000) << "after a pause, not in a tight loop";

    fireListenRetry(transport);

    ASSERT_EQ(http.streamCount(), 3);
    const auto again = http.streamRequest(2);
    EXPECT_EQ(again.verb, QByteArray("GET"));
    EXPECT_EQ(again.header("Mcp-Session-Id"), QByteArray("sess-42"));
    EXPECT_EQ(again.header("Last-Event-ID"), QByteArray("7"))
        << "the server can resume where the old stream stopped";
}

TEST_F(McpHttpServerTest, LatestSpecRestartsTheListenBackoffAfterASteadyStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendFinished();
    QTimer *retry = listenRetryTimer(transport);
    ASSERT_TRUE(retry);
    EXPECT_EQ(retry->interval(), 1000);

    fireListenRetry(transport);
    http.failLastStream("Connection refused", QNetworkReply::ConnectionRefusedError);
    EXPECT_EQ(retry->interval(), 2000) << "attempts that keep failing back off";

    fireListenRetry(transport);
    ASSERT_EQ(http.streamCount(), 4);
    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    pumpEventLoop(std::chrono::milliseconds(1100));
    http.lastStream()->sendFinished();

    EXPECT_EQ(retry->interval(), 1000)
        << "a quiet stream that stayed up is healthy, whatever cut it in the end";

    fireListenRetry(transport);
    http.failLastStream("Connection refused", QNetworkReply::ConnectionRefusedError);
    EXPECT_EQ(retry->interval(), 2000) << "the failure after it counts as a failure again";
}

TEST_F(McpHttpServerTest, LatestSpecBacksOffFromAServerThatClosesEveryListenStreamAtOnce)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    for (const int expected : {1000, 2000, 4000}) {
        http.lastStream()->sendHeaders(200, eventStreamHeaders());
        http.lastStream()->sendFinished();
        ASSERT_TRUE(listenRetryTimer(transport));
        ASSERT_TRUE(listenRetryTimer(transport)->isActive());
        EXPECT_EQ(listenRetryTimer(transport)->interval(), expected)
            << "accepting and closing at once is not a working stream";
        fireListenRetry(transport);
    }
}

TEST_F(McpHttpServerTest, LatestSpecResumesFromAPrimingEvent)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());
    listen->sendChunk("id: 41\ndata:\n\n");
    listen->sendFinished();
    fireListenRetry(transport);

    ASSERT_EQ(http.streamCount(), 3);
    EXPECT_EQ(http.streamRequest(2).header("Last-Event-ID"), QByteArray("41"))
        << "an event id with empty data is how a server primes the client to resume";
}

TEST_F(McpHttpServerTest, LatestSpecWaitsAsLongAsTheServerAsksBeforeReconnecting)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());
    listen->sendChunk("id: 1\nretry: 5000\ndata:\n\n");
    listen->sendFinished();

    QTimer *retry = listenRetryTimer(transport);
    ASSERT_TRUE(retry);
    ASSERT_TRUE(retry->isActive());
    EXPECT_EQ(retry->interval(), 5000) << "the client MUST respect the retry field";
}

TEST_F(McpHttpServerTest, LatestSpecClearsTheEventCursorOnAnEmptyId)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    const QByteArray notification
        = compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/message"}});
    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());
    listen->sendChunk("id: 7\ndata: " + notification + "\n\nid:\ndata: " + notification + "\n\n");
    listen->sendFinished();
    fireListenRetry(transport);

    ASSERT_EQ(http.streamCount(), 3);
    EXPECT_FALSE(http.streamRequest(2).request.hasRawHeader("Last-Event-ID"));
}

TEST_F(McpHttpServerTest, LatestSpecKeepsTheEventCursorThroughEventsWithoutIds)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    const QByteArray notification
        = compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/message"}});
    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendChunk("id: 7\ndata: " + notification + "\n\n");
    http.lastStream()->sendFinished();
    fireListenRetry(transport);
    ASSERT_EQ(http.streamCount(), 3);

    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendChunk(": keep-alive\n\ndata: " + notification + "\n\n");
    http.lastStream()->sendFinished();
    fireListenRetry(transport);

    ASSERT_EQ(http.streamCount(), 4);
    EXPECT_EQ(http.streamRequest(3).header("Last-Event-ID"), QByteArray("7"));
}

TEST_F(McpHttpServerTest, LatestSpecForgetsAnEventCursorTheServerDoesNotResume)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendChunk(
        "id: 7\nevent: message\ndata: "
        + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/message"}})
        + "\n\n");
    http.lastStream()->sendFinished();
    fireListenRetry(transport);
    ASSERT_EQ(http.streamRequest(2).header("Last-Event-ID"), QByteArray("7"));

    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendFinished();
    fireListenRetry(transport);

    ASSERT_EQ(http.streamCount(), 4);
    EXPECT_FALSE(http.streamRequest(3).request.hasRawHeader("Last-Event-ID"))
        << "a resumed stream that closes at once with nothing in it cannot resume that cursor";
}

TEST_F(McpHttpServerTest, LatestSpecKeepsListenEventsInOrderWhenAHandlerReenters)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());

    QStringList order;
    QObject::connect(
        &transport, &Rpc::Transport::messageReceived, &transport, [&](const QJsonObject &message) {
            order.append(message.value("method").toString());
            if (order.size() == 1)
                listen->sendChunk(listenEvent("third"));
        });
    listen->sendChunk(listenEvent("first") + listenEvent("second"));

    EXPECT_EQ(order.join(' ').toStdString(), "first second third")
        << "a chunk that arrives while a handler runs, as in a modal dialog, must queue";
}

TEST_F(McpHttpServerTest, LatestSpecDeliversNoListenEventAfterStop)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());

    QStringList order;
    QObject::connect(
        &transport, &Rpc::Transport::messageReceived, &transport, [&](const QJsonObject &message) {
            order.append(message.value("method").toString());
            transport.stop();
        });
    listen->sendChunk(listenEvent("first") + listenEvent("second"));

    EXPECT_EQ(order.join(' ').toStdString(), "first") << "the session ended with the first";
}

TEST_F(McpHttpServerTest, LatestSpecResumesAfterTheLastEventReceivedNotDelivered)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());

    QStringList order;
    QObject::connect(
        &transport, &Rpc::Transport::messageReceived, &transport, [&](const QJsonObject &message) {
            order.append(message.value("method").toString());
            if (order.size() != 1)
                return;
            listen->sendFinished();
            fireListenRetry(transport);
        });
    listen->sendChunk(listenEvent("first", "1") + listenEvent("second", "2"));

    ASSERT_EQ(http.streamCount(), 3);
    EXPECT_EQ(http.streamRequest(2).header("Last-Event-ID"), QByteArray("2"))
        << "the server would otherwise replay an event that is already on its way";
    EXPECT_EQ(order.join(' ').toStdString(), "first second");
}

TEST_F(McpHttpServerTest, LatestSpecReportsAListenOutageOnceUntilItRecovers)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);
    QtWarningCapture capture;
    for (int attempt = 0; attempt < 7; ++attempt) {
        http.failLastStream("Connection refused", QNetworkReply::ConnectionRefusedError);
        fireListenRetry(transport);
    }
    EXPECT_EQ(errors.size(), 1) << "otherwise a deaf session looks exactly like a quiet one";
    EXPECT_EQ(capture.warnings().filter("Server message stream").size(), 1);

    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendChunk(listenEvent("notifications/message"));
    http.lastStream()->sendFinished();
    fireListenRetry(transport);
    for (int attempt = 0; attempt < 5; ++attempt) {
        http.failLastStream("Connection refused", QNetworkReply::ConnectionRefusedError);
        fireListenRetry(transport);
    }
    EXPECT_EQ(errors.size(), 2) << "an outage after a recovery is a new one";
}

TEST_F(McpHttpServerTest, LatestSpecClosesTheListenStreamOnStop)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    const QPointer<FakeHttpStream> listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());

    transport.stop();

    ASSERT_TRUE(listen);
    EXPECT_TRUE(listen->isAborted()) << "a stopped transport must not keep a connection open";
}

TEST_F(McpHttpServerTest, LatestSpecRetriesAListenStreamTheServerRefusesForNow)
{
    for (const int status : {409, 429, 503}) {
        SCOPED_TRACE(status);
        FakeHttpTransport http;

        HttpTransportConfig cfg;
        cfg.endpoint = QUrl("http://mcp.local/mcp");
        McpStreamableHttpTransport transport(cfg, &http);
        ASSERT_TRUE(openSession(transport, http));

        http.lastStream()->sendHeaders(200, eventStreamHeaders());
        http.lastStream()->sendFinished();
        fireListenRetry(transport);
        ASSERT_EQ(http.streamCount(), 3);

        QtWarningCapture capture;
        http.respondToLastStream(status, "busy", {{"Content-Type", "text/plain"}});

        ASSERT_TRUE(listenRetryTimer(transport));
        EXPECT_TRUE(listenRetryTimer(transport)->isActive())
            << "the old stream may still be registered, or the server is briefly away";
        EXPECT_TRUE(capture.warnings().isEmpty());

        fireListenRetry(transport);
        EXPECT_EQ(http.streamCount(), 4);
    }
}

TEST_F(McpHttpServerTest, LatestSpecEndsTheSessionWhenTheListenStreamFindsItGone)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendFinished();
    fireListenRetry(transport);
    ASSERT_EQ(http.streamCount(), 3);

    QSignalSpy closed(&transport, &Rpc::Transport::closed);
    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

    http.respondToLastStream(
        404,
        compact(QJsonObject{
            {"jsonrpc", "2.0"},
            {"error", QJsonObject{{"code", -32001}, {"message", "Session not found"}}},
            {"id", QJsonValue::Null},
        }));

    EXPECT_EQ(closed.size(), 1) << "a 404 to a request carrying the session means it is gone";
    EXPECT_EQ(errors.size(), 1);
    EXPECT_FALSE(transport.isOpen());
    EXPECT_TRUE(transport.sessionId().isEmpty());
}

TEST_F(McpHttpServerTest, LatestSpecStopsAskingWhenTheFirstListenStreamIsNotFound)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy closed(&transport, &Rpc::Transport::closed);

    http.respondToLastStream(404, "Not Found", {{"Content-Type", "text/plain"}});

    EXPECT_TRUE(closed.isEmpty()) << "a server that routes only POST answers a GET with 404";
    EXPECT_TRUE(transport.isOpen());
    ASSERT_TRUE(listenRetryTimer(transport));
    EXPECT_FALSE(listenRetryTimer(transport)->isActive());
}

TEST_F(McpHttpServerTest, LatestSpecDropsAnEventCursorTheServerNoLongerKnows)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());
    listen->sendChunk(
        "id: 7\nevent: message\ndata: "
        + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/message"}})
        + "\n\n");
    listen->sendFinished();
    fireListenRetry(transport);
    ASSERT_EQ(http.streamCount(), 3);
    ASSERT_EQ(http.streamRequest(2).header("Last-Event-ID"), QByteArray("7"));

    http.respondToLastStream(
        400,
        compact(QJsonObject{
            {"jsonrpc", "2.0"},
            {"error", QJsonObject{{"code", -32000}, {"message", "Invalid event ID format"}}},
            {"id", QJsonValue::Null},
        }));
    fireListenRetry(transport);

    ASSERT_EQ(http.streamCount(), 4);
    EXPECT_FALSE(http.streamRequest(3).request.hasRawHeader("Last-Event-ID"))
        << "a cursor the server cannot resolve would be refused on every reconnect";
}

TEST_F(McpHttpServerTest, LatestSpecStartsEachSessionWithAFreshListenStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());
    listen->sendChunk(
        "id: 7\nevent: message\ndata: "
        + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/message"}})
        + "\n\n");
    listen->sendFinished();
    fireListenRetry(transport);
    http.respondToLastStream(405, "Method Not Allowed", {{"Content-Type", "text/plain"}});
    ASSERT_EQ(http.streamCount(), 3);

    transport.stop();
    transport.start();
    transport.send(jsonRpcRequest(2, "initialize"));
    http.respondToLastStream(200, compact(initializeReply(2)), sessionHeaders("sess-43"));
    spin();

    ASSERT_EQ(http.streamCount(), 5) << "a refusal belongs to the session that got it";
    EXPECT_FALSE(http.streamRequest(4).request.hasRawHeader("Last-Event-ID"));

    QSignalSpy closed(&transport, &Rpc::Transport::closed);
    http.respondToLastStream(404, "Not Found", {{"Content-Type", "text/plain"}});
    EXPECT_TRUE(closed.isEmpty()) << "the new session has not had a listen stream yet";
}

TEST_F(McpHttpServerTest, LatestSpecStartsANewSessionWithEveryInitialize)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    const QPointer<FakeHttpStream> listen = http.lastStream();
    listen->sendHeaders(200, eventStreamHeaders());
    listen->sendChunk(
        "id: 7\nevent: message\ndata: "
        + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/message"}})
        + "\n\n");
    transport.send(jsonRpcRequest(2, "tools/call"));

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);
    transport.send(jsonRpcRequest(3, "initialize"));

    const auto reinitialize = http.lastStreamRequest("POST");
    EXPECT_FALSE(reinitialize.request.hasRawHeader("Mcp-Session-Id"))
        << "a new InitializeRequest goes without a session ID, or the server refuses it";
    EXPECT_FALSE(reinitialize.request.hasRawHeader("MCP-Protocol-Version"));
    ASSERT_EQ(failed.size(), 1) << "a call from the old session cannot be answered any more";
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 2);
    ASSERT_TRUE(listen);
    EXPECT_TRUE(listen->isAborted());

    http.respondToLastStream(200, compact(initializeReply(3)), sessionHeaders("sess-43"));
    spin();

    EXPECT_EQ(transport.sessionId(), QString("sess-43"));
    const auto listenAgain = http.lastStreamRequest("GET");
    EXPECT_EQ(listenAgain.header("Mcp-Session-Id"), QByteArray("sess-43"));
    EXPECT_FALSE(listenAgain.request.hasRawHeader("Last-Event-ID"))
        << "an event cursor belongs to the session that issued it";
}

TEST_F(McpHttpServerTest, LatestSpecForgetsAPendingReconnectOnStop)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendFinished();
    QTimer *retry = listenRetryTimer(transport);
    ASSERT_TRUE(retry);
    ASSERT_TRUE(retry->isActive());

    transport.stop();
    transport.start();

    EXPECT_FALSE(retry->isActive())
        << "a GET left over from the old session would go out without one and be refused";
}

TEST_F(McpHttpServerTest, LatestSpecReconnectsWhenTheHttpTransportDropsTheListenStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    delete http.lastStream();

    ASSERT_TRUE(listenRetryTimer(transport));
    EXPECT_TRUE(listenRetryTimer(transport)->isActive())
        << "a stream its HttpTransport took away has ended like any other";
}

TEST_F(McpHttpServerTest, LatestSpecStopsReconnectingOnceTheHttpTransportIsDeleted)
{
    for (const bool endedFirst : {true, false}) {
        SCOPED_TRACE(endedFirst ? "listen stream ended first" : "listen stream still open");
        auto *http = new FakeHttpTransport;

        HttpTransportConfig cfg;
        cfg.endpoint = QUrl("http://mcp.local/mcp");
        McpStreamableHttpTransport transport(cfg, http);
        ASSERT_TRUE(openSession(transport, *http));

        http->lastStream()->sendHeaders(200, eventStreamHeaders());
        if (endedFirst)
            http->lastStream()->sendFinished();

        delete http;

        ASSERT_TRUE(listenRetryTimer(transport));
        EXPECT_FALSE(listenRetryTimer(transport)->isActive())
            << "a reconnect must never reach a deleted HttpTransport";
        EXPECT_TRUE(transport.isOpen());
    }
}

TEST_F(McpHttpServerTest, LatestSpecFailsASendOnceTheHttpTransportIsDeleted)
{
    auto *http = new FakeHttpTransport;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, http);
    transport.start();
    delete http;

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);
    transport.send(jsonRpcRequest(1, "ping"));

    ASSERT_EQ(failed.size(), 1) << "the request fails instead of reaching a deleted HttpTransport";
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 1);
}

TEST_F(McpHttpServerTest, LatestSpecFailsARequestTheHttpTransportOpensNoStreamFor)
{
    FakeHttpTransport http;
    http.refuseStreams("POST");

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    transport.start();

    QtWarningCapture capture;
    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);
    transport.send(jsonRpcRequest(1, "tools/call"));

    ASSERT_EQ(failed.size(), 1) << "no stream means no reply; waiting for one only times out";
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 1);
    EXPECT_TRUE(capture.warnings().filter("nullptr").isEmpty())
        << capture.warnings().join('\n').toStdString();
}

TEST_F(McpHttpServerTest, LatestSpecRetriesAListenStreamTheHttpTransportOpensNoStreamFor)
{
    FakeHttpTransport http;
    http.refuseStreams("GET");

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));

    QtWarningCapture capture;
    http.respondToLastStream(200, compact(initializeReply(1)), sessionHeaders("sess-42"));
    spin();

    EXPECT_EQ(http.refusedStreams(), 1);
    ASSERT_TRUE(listenRetryTimer(transport));
    EXPECT_TRUE(listenRetryTimer(transport)->isActive()) << "treated like a dropped stream";
    EXPECT_TRUE(capture.warnings().filter("nullptr").isEmpty())
        << capture.warnings().join('\n').toStdString();
}

TEST_F(McpHttpServerTest, LatestSpecKeepsItsListenRetryTimerOnItsOwnThread)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    QThread worker;
    McpStreamableHttpTransport transport(cfg, &http);

    transport.moveToThread(&worker);

    ASSERT_TRUE(listenRetryTimer(transport));
    EXPECT_EQ(listenRetryTimer(transport)->thread(), &worker)
        << "a timer left behind cannot be started from the transport's new thread";
}

TEST_F(McpHttpServerTest, LatestSpecFailsARequestWhoseStreamTheHttpTransportDeletes)
{
    auto *http = new FakeHttpTransport;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, http);

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    transport.start();
    transport.send(jsonRpcRequest(1, "tools/call"));
    delete http;

    ASSERT_EQ(failed.size(), 1)
        << "a reply that can no longer arrive must not leave the call hanging";
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 1);
}

TEST_F(McpHttpServerTest, FactoryBuildsTheSseTransportForTheLegacySpec)
{
    FakeHttpTransport http;
    QObject owner;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    cfg.spec = McpHttpSpec::V2024_11_05;
    cfg.requestTimeoutMs = 7000;

    Rpc::Transport *transport = makeHttpTransport(cfg, &http, &owner);

    auto *sse = qobject_cast<McpSseHttpTransport *>(transport);
    ASSERT_NE(sse, nullptr);
    EXPECT_EQ(transport->parent(), &owner);
    EXPECT_EQ(sse->config().requestTimeoutMs, 7000);

    transport->start();
    EXPECT_EQ(http.streamCount(), 1) << "the injected HttpTransport must carry the traffic";
}

TEST_F(McpHttpServerTest, FactoryBuildsTheStreamableTransportForTheCurrentSpec)
{
    FakeHttpTransport http;
    QObject owner;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.spec = McpHttpSpec::V2025_03_26;

    Rpc::Transport *transport = makeHttpTransport(cfg, &http, &owner);

    ASSERT_NE(qobject_cast<McpStreamableHttpTransport *>(transport), nullptr);
    EXPECT_EQ(transport->parent(), &owner);

    transport->start();
    transport->send(jsonRpcRequest(1, "initialize"));
    EXPECT_EQ(http.bufferedCount() + http.streamCount(), 1)
        << "the injected HttpTransport must carry the traffic";
}

TEST_F(McpHttpServerTest, EachHttpTransportReportsItsOwnWireRevision)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");

    cfg.spec = McpHttpSpec::V2025_03_26;
    McpSseHttpTransport sse(cfg, &http);
    EXPECT_EQ(sse.config().spec, McpHttpSpec::V2024_11_05)
        << "config() must describe the wire the object speaks";

    cfg.spec = McpHttpSpec::V2024_11_05;
    McpStreamableHttpTransport streamable(cfg, &http);
    EXPECT_EQ(streamable.config().spec, McpHttpSpec::V2025_03_26);
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
    http.failLastStream("connection reset");
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
        http.respondToLastStream(
            outcome.status, outcome.body, {{"Content-Type", outcome.contentType}});
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
    http.respondToLastStream(400, compact(serverError), {{"Content-Type", "application/json"}});
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
    http.respondToLastStream(202, {});

    transport.send(jsonRpcRequest(2, "ping"));
    http.respondToLastStream(200, compact(jsonRpcResult(2, "pong")));

    transport.send(jsonRpcRequest(3, "tools/call"));
    const QByteArray sseBody
        = "event: message\ndata: "
          + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/progress"}})
          + "\n\nevent: message\ndata: " + compact(jsonRpcResult(3, "done")) + "\n\n";
    http.respondToLastStream(200, sseBody, {{"Content-Type", "text/event-stream"}});
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
    http.respondToLastStream(200, compact(jsonRpcResult(3, "pong")));
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
    http.respondToLastStream(200, sseBody, {{"Content-Type", "text/event-stream"}});
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
    http.respondToLastStream(202, {});
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
    http.respondToLastStream(503, "upstream down");
    spin();

    ASSERT_EQ(errors.size(), 1);
    EXPECT_TRUE(errors.first().first().toString().contains("503"));
}

namespace {

QHash<QString, QString> headersImpersonatingTheProtocol()
{
    return {
        {"accept", "application/json"},
        {"Content-Type", "text/plain"},
        {"MCP-SESSION-ID", "forged"},
        {"mcp-protocol-version", "1999-01-01"},
        {"Last-Event-Id", "forged-cursor"},
        {"Cache-Control", "max-age=60"},
        {"Authorization", "Bearer secret"},
    };
}

} // namespace

TEST_F(McpHttpServerTest, LatestSpecKeepsProtocolHeadersOutOfTheConfiguredHeaders)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.headers = headersImpersonatingTheProtocol();
    McpStreamableHttpTransport transport(cfg, &http);

    ASSERT_TRUE(openSession(transport, http));

    const auto initialize = http.streamRequest(0);
    EXPECT_EQ(initialize.header("Accept"), QByteArray("application/json, text/event-stream"));
    EXPECT_EQ(initialize.header("Content-Type"), QByteArray("application/json"));
    EXPECT_TRUE(initialize.header("Mcp-Session-Id").isEmpty());
    EXPECT_TRUE(initialize.header("MCP-Protocol-Version").isEmpty());
    EXPECT_TRUE(initialize.header("Last-Event-ID").isEmpty());
    EXPECT_TRUE(initialize.header("Cache-Control").isEmpty());
    EXPECT_EQ(initialize.header("Authorization"), QByteArray("Bearer secret"));

    const auto listen = http.streamRequest(1);
    EXPECT_EQ(listen.header("Accept"), QByteArray("text/event-stream"));
    EXPECT_EQ(listen.header("Cache-Control"), QByteArray("no-cache"));
    EXPECT_EQ(listen.header("Mcp-Session-Id"), QByteArray("sess-42"));
    EXPECT_EQ(listen.header("MCP-Protocol-Version"), QByteArray("2025-06-18"));
    EXPECT_TRUE(listen.header("Last-Event-ID").isEmpty());
    EXPECT_EQ(listen.header("Authorization"), QByteArray("Bearer secret"));

    transport.send(jsonRpcRequest(2, "tools/list"));
    const auto later = http.lastStreamRequest("POST");
    EXPECT_EQ(later.header("Accept"), QByteArray("application/json, text/event-stream"));
    EXPECT_EQ(later.header("Content-Type"), QByteArray("application/json"));
    EXPECT_EQ(later.header("Mcp-Session-Id"), QByteArray("sess-42"));
    EXPECT_EQ(later.header("MCP-Protocol-Version"), QByteArray("2025-06-18"));
    EXPECT_TRUE(later.header("Cache-Control").isEmpty());
    EXPECT_EQ(later.header("Authorization"), QByteArray("Bearer secret"));
}

TEST_F(McpHttpServerTest, LegacySpecKeepsProtocolHeadersOutOfTheConfiguredHeaders)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/sse");
    cfg.headers = headersImpersonatingTheProtocol();
    McpSseHttpTransport transport(cfg, &http);

    transport.start();
    ASSERT_EQ(http.streamCount(), 1);
    const auto stream = http.streamRequest(0);
    EXPECT_EQ(stream.header("Accept"), QByteArray("text/event-stream"));
    EXPECT_EQ(stream.header("Cache-Control"), QByteArray("no-cache"));
    EXPECT_TRUE(stream.header("Content-Type").isEmpty());
    EXPECT_TRUE(stream.header("Mcp-Session-Id").isEmpty());
    EXPECT_TRUE(stream.header("MCP-Protocol-Version").isEmpty());
    EXPECT_TRUE(stream.header("Last-Event-ID").isEmpty());
    EXPECT_EQ(stream.header("Authorization"), QByteArray("Bearer secret"));

    http.lastStream()->sendChunk("event: endpoint\ndata: /messages?sessionId=abc\n\n");
    transport.send(jsonRpcRequest(1, "ping"));
    spin();

    ASSERT_EQ(http.bufferedCount(), 1);
    const auto posted = http.bufferedRequest(0);
    EXPECT_EQ(posted.header("Content-Type"), QByteArray("application/json"));
    EXPECT_TRUE(posted.header("Accept").isEmpty());
    EXPECT_TRUE(posted.header("Cache-Control").isEmpty());
    EXPECT_TRUE(posted.header("Mcp-Session-Id").isEmpty());
    EXPECT_TRUE(posted.header("MCP-Protocol-Version").isEmpty());
    EXPECT_TRUE(posted.header("Last-Event-ID").isEmpty());
    EXPECT_EQ(posted.header("Authorization"), QByteArray("Bearer secret"));
}

TEST_F(McpHttpServerTest, EachHttpTransportWarnsOnceAboutTheProtocolHeadersItIgnores)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.headers = headersImpersonatingTheProtocol();

    {
        QtWarningCapture capture;
        McpStreamableHttpTransport transport(cfg, &http);
        ASSERT_TRUE(openSession(transport, http));
        transport.send(jsonRpcRequest(2, "tools/list"));
        spin();
        ASSERT_EQ(capture.warnings().size(), 1) << capture.warnings().join('\n').toStdString();
        const QString warning = capture.warnings().first();
        for (const char *name : {"accept", "Content-Type", "MCP-SESSION-ID",
                                 "mcp-protocol-version", "Last-Event-Id", "Cache-Control"})
            EXPECT_TRUE(warning.contains(QLatin1String(name))) << name;
        EXPECT_FALSE(warning.contains(QLatin1String("Authorization")));
    }
    {
        QtWarningCapture capture;
        McpSseHttpTransport transport(cfg, &http);
        transport.start();
        http.lastStream()->sendChunk("event: endpoint\ndata: /messages\n\n");
        transport.send(jsonRpcRequest(1, "ping"));
        spin();
        EXPECT_EQ(capture.warnings().size(), 1) << capture.warnings().join('\n').toStdString();
    }
    {
        cfg.headers = {{"Authorization", "Bearer secret"}, {"X-Tenant", "acme"}};
        QtWarningCapture capture;
        McpStreamableHttpTransport streamable(cfg, &http);
        McpSseHttpTransport sse(cfg, &http);
        EXPECT_TRUE(capture.warnings().isEmpty()) << capture.warnings().join('\n').toStdString();
    }
}

TEST_F(McpHttpServerTest, LatestSpecDropsTheStreamOfAnAbandonedRequest)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    transport.send(jsonRpcRequest(2, "tools/call"));
    FakeHttpStream *abandoned = http.lastStream();
    transport.send(jsonRpcRequest(3, "tools/call"));
    FakeHttpStream *kept = http.lastStream();
    ASSERT_NE(abandoned, kept);
    abandoned->sendHeaders(200, eventStreamHeaders());
    kept->sendHeaders(200, eventStreamHeaders());

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);
    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);
    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);
    QtWarningCapture capture;

    transport.abandon(QStringLiteral("2"));
    spin();

    EXPECT_TRUE(abandoned->isAborted()) << "an abandoned request must give its connection back";
    EXPECT_FALSE(kept->isAborted());

    abandoned->sendChunk("data: " + compact(jsonRpcResult(2, "late")) + "\n\n");
    abandoned->sendFinished();
    kept->sendChunk("data: " + compact(jsonRpcResult(3, "on time")) + "\n\n");
    kept->sendFinished();
    spin();

    ASSERT_EQ(messages.size(), 1) << "a late answer to an abandoned request must be ignored";
    EXPECT_EQ(messages.first().first().toJsonObject().value("id").toInt(), 3);
    EXPECT_EQ(failed.size(), 0) << "the caller already knows how an abandoned request ended";
    EXPECT_EQ(errors.size(), 0);
    EXPECT_TRUE(capture.warnings().isEmpty()) << capture.warnings().join('\n').toStdString();
}

TEST_F(McpHttpServerTest, LatestSpecIgnoresAbandoningARequestItDoesNotKnow)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    transport.send(jsonRpcRequest(2, "tools/call"));
    FakeHttpStream *pending = http.lastStream();
    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/progress"}});
    FakeHttpStream *notification = http.lastStream();

    transport.abandon(QStringLiteral("7"));
    transport.abandon(QString());
    spin();

    EXPECT_FALSE(pending->isAborted());
    EXPECT_FALSE(notification->isAborted());
}

TEST_F(McpHttpServerTest, CancellingAToolCallFreesItsHttpConnection)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    auto *transport = new McpStreamableHttpTransport(cfg, &http);
    McpClient client(transport, Implementation{"cancel-client", "0.0.1"});

    const auto init = client.connectAndInitialize(std::chrono::seconds(5));
    spin();
    ASSERT_EQ(http.streamCount(), 1);
    http.respondToLastStream(200, compact(QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", http.streamRequest(0).payload().value("id")},
        {"result", QJsonObject{
            {"protocolVersion", "2025-06-18"},
            {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
            {"serverInfo", QJsonObject{{"name", "s"}, {"version", "1"}}},
        }},
    }), sessionHeaders("sess-42"));
    spin();
    ASSERT_TRUE(init.isFinished());

    const auto call = client.callToolWithProgress("slow", {}, {});
    spin();
    FakeHttpStream *callStream = nullptr;
    for (int i = 0; i < http.streamCount(); ++i) {
        if (http.streamRequest(i).payload().value("method").toString() == "tools/call")
            callStream = http.streamAt(i);
    }
    ASSERT_NE(callStream, nullptr);
    callStream->sendHeaders(200, eventStreamHeaders());

    client.cancel(call.requestId);
    spin();

    EXPECT_TRUE(callStream->isAborted());
    EXPECT_EQ(http.lastStreamRequest("POST").payload().value("method").toString().toStdString(),
              "notifications/cancelled");
}

TEST_F(McpHttpServerTest, LatestSpecDeletesTheSessionOnStop)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.headers.insert("Authorization", "Bearer secret");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));
    ASSERT_EQ(http.bufferedCount(), 0);

    transport.stop();

    ASSERT_EQ(http.bufferedCount(), 1) << "a client done with a session should tell the server";
    const auto sent = http.bufferedRequest(0);
    EXPECT_EQ(sent.verb, QByteArray("DELETE"));
    EXPECT_EQ(sent.url(), cfg.endpoint);
    EXPECT_EQ(sent.header("Mcp-Session-Id"), QByteArray("sess-42"));
    EXPECT_EQ(sent.header("MCP-Protocol-Version"), QByteArray("2025-06-18"));
    EXPECT_EQ(sent.header("Authorization"), QByteArray("Bearer secret"));
    EXPECT_GT(sent.request.transferTimeout(), 0);
    EXPECT_LT(sent.request.transferTimeout(), cfg.requestTimeoutMs)
        << "a goodbye must not hold a connection as long as a request may";
}

TEST_F(McpHttpServerTest, LatestSpecDeletesTheOldSessionOnReinitialize)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    transport.send(jsonRpcRequest(3, "initialize"));

    ASSERT_EQ(http.bufferedCount(), 1);
    EXPECT_EQ(http.bufferedRequest(0).verb, QByteArray("DELETE"));
    EXPECT_EQ(http.bufferedRequest(0).header("Mcp-Session-Id"), QByteArray("sess-42"));
    EXPECT_FALSE(http.lastStreamRequest("POST").request.hasRawHeader("Mcp-Session-Id"));
}

TEST_F(McpHttpServerTest, LatestSpecDeletesTheSessionWhenItIsDestroyed)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    {
        McpStreamableHttpTransport transport(cfg, &http);
        ASSERT_TRUE(openSession(transport, http));
    }

    ASSERT_EQ(http.bufferedCount(), 1) << "an injected HttpTransport outlives the session";
    EXPECT_EQ(http.bufferedRequest(0).verb, QByteArray("DELETE"));
}

TEST_F(McpHttpServerTest, LatestSpecDoesNotDeleteASessionTheServerEnded)
{
    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");

    {
        FakeHttpTransport http;
        McpStreamableHttpTransport transport(cfg, &http);
        ASSERT_TRUE(openSession(transport, http));
        transport.send(jsonRpcRequest(2, "tools/list"));
        http.respondToLastStream(404, {});
        spin();
        ASSERT_FALSE(transport.isOpen());
        EXPECT_EQ(http.bufferedCount(), 0) << "a 404 to a POST: the server already ended it";
    }
    {
        FakeHttpTransport http;
        McpStreamableHttpTransport transport(cfg, &http);
        ASSERT_TRUE(openSession(transport, http));
        http.lastStream()->sendHeaders(200, eventStreamHeaders());
        http.lastStream()->sendFinished();
        fireListenRetry(transport);
        http.respondToLastStream(404, {});
        spin();
        ASSERT_FALSE(transport.isOpen());
        EXPECT_EQ(http.bufferedCount(), 0) << "a 404 to the listen stream: the server ended it";
    }
}

TEST_F(McpHttpServerTest, LatestSpecDoesNotDeleteWithoutASession)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    transport.stop();

    EXPECT_EQ(http.bufferedCount(), 0);
}

TEST_F(McpHttpServerTest, LatestSpecIgnoresARefusedDelete)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);
    QtWarningCapture capture;

    transport.stop();
    ASSERT_EQ(http.bufferedCount(), 1);
    http.respondToLast(405, {});
    spin();

    transport.start();
    transport.send(jsonRpcRequest(1, "initialize"));
    http.respondToLastStream(200, compact(initializeReply(1)), sessionHeaders("sess-43"));
    spin();
    transport.stop();
    ASSERT_EQ(http.bufferedCount(), 2);
    http.failLast(QStringLiteral("connection refused"));
    spin();

    EXPECT_EQ(errors.size(), 0) << "the server MAY refuse to end a session; that is not an error";
    EXPECT_TRUE(capture.warnings().isEmpty()) << capture.warnings().join('\n').toStdString();
}

namespace {

QList<QTimer *> activeResumeTimers(const QObject &transport)
{
    QList<QTimer *> active;
    for (QTimer *timer : transport.findChildren<QTimer *>(
             QStringLiteral("resumeTimer"), Qt::FindDirectChildrenOnly)) {
        if (timer->isActive())
            active.append(timer);
    }
    return active;
}

void fireResume(const QObject &transport)
{
    for (QTimer *timer : activeResumeTimers(transport))
        timer->start(0);
    spin();
}

QByteArray resultEvent(int id, const QString &value, const QByteArray &eventId)
{
    return "id: " + eventId + "\ndata: " + compact(jsonRpcResult(id, value)) + "\n\n";
}

FakeHttpStream *callWithPrimedStream(
    McpStreamableHttpTransport &transport, FakeHttpTransport &http, const QByteArray &priming)
{
    transport.send(jsonRpcRequest(2, "tools/call"));
    FakeHttpStream *post = http.lastStream();
    post->sendHeaders(200, eventStreamHeaders());
    post->sendChunk(priming);
    return post;
}

} // namespace

TEST_F(McpHttpServerTest, LatestSpecResumesARequestWhoseStreamClosedBeforeTheAnswer)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    cfg.headers.insert("Authorization", "Bearer secret");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);
    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);
    QSignalSpy errors(&transport, &Rpc::Transport::errorOccurred);

    callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
    spin();

    EXPECT_EQ(failed.size(), 0) << "a disconnection is not a cancellation of the request";
    ASSERT_EQ(activeResumeTimers(transport).size(), 1);
    const int streamsBefore = http.streamCount();
    fireResume(transport);
    ASSERT_EQ(http.streamCount(), streamsBefore + 1);

    const auto resumed = http.lastStreamRequest("GET");
    EXPECT_EQ(resumed.url(), cfg.endpoint);
    EXPECT_EQ(resumed.header("Last-Event-ID"), QByteArray("s1"));
    EXPECT_EQ(resumed.header("Accept"), QByteArray("text/event-stream"));
    EXPECT_EQ(resumed.header("Mcp-Session-Id"), QByteArray("sess-42"));
    EXPECT_EQ(resumed.header("MCP-Protocol-Version"), QByteArray("2025-06-18"));
    EXPECT_EQ(resumed.header("Authorization"), QByteArray("Bearer secret"));

    FakeHttpStream *stream = http.lastStream();
    stream->sendHeaders(200, eventStreamHeaders());
    stream->sendChunk(resultEvent(2, "late but delivered", "s2"));
    stream->sendFinished();
    spin();

    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().first().toJsonObject().value("id").toInt(), 2);
    EXPECT_EQ(failed.size(), 0);
    EXPECT_EQ(errors.size(), 0);
    EXPECT_TRUE(activeResumeTimers(transport).isEmpty());
}

TEST_F(McpHttpServerTest, LatestSpecResumesARequestWhoseStreamBrokeWithANetworkError)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")
        ->sendError(QStringLiteral("connection reset"), QNetworkReply::RemoteHostClosedError);
    spin();

    EXPECT_EQ(failed.size(), 0);
    fireResume(transport);
    EXPECT_EQ(http.lastStreamRequest("GET").header("Last-Event-ID"), QByteArray("s1"));
}

TEST_F(McpHttpServerTest, LatestSpecWaitsAsLongAsTheServerAsksBeforeResuming)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    callWithPrimedStream(transport, http, "retry: 5000\nid: s1\ndata: \n\n")->sendFinished();
    spin();

    const QList<QTimer *> timers = activeResumeTimers(transport);
    ASSERT_EQ(timers.size(), 1);
    EXPECT_GE(timers.first()->interval(), 5000) << "the client MUST respect the retry field";
}

TEST_F(McpHttpServerTest, LatestSpecBacksOffBetweenResumptionsThatBringNothing)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
    spin();
    ASSERT_EQ(activeResumeTimers(transport).size(), 1);
    const int first = activeResumeTimers(transport).first()->interval();

    fireResume(transport);
    http.lastStream()->sendHeaders(200, eventStreamHeaders());
    http.lastStream()->sendFinished();
    spin();

    ASSERT_EQ(activeResumeTimers(transport).size(), 1) << "a server may close every poll";
    EXPECT_GT(activeResumeTimers(transport).first()->interval(), first);
    fireResume(transport);
    EXPECT_EQ(http.lastStreamRequest("GET").header("Last-Event-ID"), QByteArray("s1"));
}

TEST_F(McpHttpServerTest, LatestSpecResumesFromTheLastEventOfTheResumedStream)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);

    callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
    fireResume(transport);
    FakeHttpStream *resumed = http.lastStream();
    resumed->sendHeaders(200, eventStreamHeaders());
    resumed->sendChunk(
        "id: s2\ndata: "
        + compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/progress"}})
        + "\n\n");
    resumed->sendFinished();
    spin();

    EXPECT_EQ(messages.size(), 1);
    fireResume(transport);
    EXPECT_EQ(http.lastStreamRequest("GET").header("Last-Event-ID"), QByteArray("s2"));
}

TEST_F(McpHttpServerTest, LatestSpecFailsARequestWhoseResumptionTheServerRejects)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
    fireResume(transport);
    http.respondToLastStream(400, "Invalid event ID format", {{"Content-Type", "text/plain"}});
    spin();

    ASSERT_EQ(failed.size(), 1);
    EXPECT_EQ(failed.first().at(0).toJsonObject().value("id").toInt(), 2);
    EXPECT_TRUE(transport.isOpen()) << "one lost request does not end the session";
    EXPECT_TRUE(activeResumeTimers(transport).isEmpty());
}

TEST_F(McpHttpServerTest, LatestSpecRetriesAResumptionTheServerRefusesForNow)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
    fireResume(transport);
    http.respondToLastStream(409, "Stream already has an active connection");
    spin();

    EXPECT_EQ(failed.size(), 0);
    ASSERT_EQ(activeResumeTimers(transport).size(), 1);
    fireResume(transport);
    EXPECT_EQ(http.lastStreamRequest("GET").header("Last-Event-ID"), QByteArray("s1"));
}

TEST_F(McpHttpServerTest, LatestSpecEndsTheSessionWhenAResumptionFindsItGone)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);
    QSignalSpy closed(&transport, &Rpc::Transport::closed);

    callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
    fireResume(transport);
    http.respondToLastStream(404, {});
    spin();

    EXPECT_EQ(failed.size(), 1);
    EXPECT_EQ(closed.size(), 1);
    EXPECT_FALSE(transport.isOpen());
    EXPECT_EQ(http.bufferedCount(), 0) << "the server ended the session; nothing to delete";
}

TEST_F(McpHttpServerTest, LatestSpecForgetsAPendingResumptionOnStopOrAbandon)
{
    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");

    {
        FakeHttpTransport http;
        McpStreamableHttpTransport transport(cfg, &http);
        ASSERT_TRUE(openSession(transport, http));
        callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
        spin();
        ASSERT_EQ(activeResumeTimers(transport).size(), 1);
        const int streams = http.streamCount();

        transport.stop();
        fireResume(transport);
        EXPECT_EQ(http.streamCount(), streams);
        EXPECT_TRUE(activeResumeTimers(transport).isEmpty());
    }
    {
        FakeHttpTransport http;
        McpStreamableHttpTransport transport(cfg, &http);
        ASSERT_TRUE(openSession(transport, http));
        QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);
        callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
        spin();
        const int streams = http.streamCount();

        transport.abandon(QStringLiteral("2"));
        fireResume(transport);
        EXPECT_EQ(http.streamCount(), streams) << "a timed-out request must not be resumed";
        EXPECT_EQ(failed.size(), 0);
    }
}

TEST_F(McpHttpServerTest, LatestSpecDoesNotResumeAnAnsweredRequestOrANotification)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    FakeHttpStream *answered = callWithPrimedStream(transport, http, "id: s1\ndata: \n\n");
    answered->sendChunk(resultEvent(2, "done", "s2"));
    answered->sendFinished();

    transport.send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    FakeHttpStream *notification = http.lastStream();
    notification->sendHeaders(200, eventStreamHeaders());
    notification->sendChunk("id: n1\ndata: \n\n");
    notification->sendFinished();
    spin();

    EXPECT_TRUE(activeResumeTimers(transport).isEmpty());
}

TEST_F(McpHttpServerTest, LatestSpecClosesAResumptionStreamOnceItCarriedTheAnswer)
{
    FakeHttpTransport http;

    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://mcp.local/mcp");
    McpStreamableHttpTransport transport(cfg, &http);
    ASSERT_TRUE(openSession(transport, http));

    QSignalSpy messages(&transport, &Rpc::Transport::messageReceived);
    QSignalSpy failed(&transport, &Rpc::Transport::sendFailed);

    callWithPrimedStream(transport, http, "id: s1\ndata: \n\n")->sendFinished();
    fireResume(transport);
    FakeHttpStream *resumed = http.lastStream();
    resumed->sendHeaders(200, eventStreamHeaders());
    resumed->sendChunk(resultEvent(2, "done", "s2"));
    spin();

    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(resumed->isAborted())
        << "a server may keep a resumed GET open after the answer; it must not hold a connection";
    EXPECT_EQ(failed.size(), 0);
}

namespace {

struct ServerUnderTest
{
    McpHttpServerTransport transport{[] {
        HttpServerConfig cfg;
        cfg.address = QHostAddress::LocalHost;
        cfg.port = 0;
        cfg.path = "/mcp";
        return cfg;
    }()};
    HttpClient http;

    QUrl endpoint() const
    {
        return QUrl(QString("http://127.0.0.1:%1/mcp").arg(transport.serverPort()));
    }

    HttpResponse request(const QByteArray &verb, const QString &session, const QByteArray &body = {})
    {
        QNetworkRequest req(endpoint());
        if (!session.isEmpty())
            req.setRawHeader("Mcp-Session-Id", session.toUtf8());
        if (!body.isEmpty())
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        return waitForFuture(http.send(req, QByteArrayView(verb), body));
    }
};

} // namespace

TEST_F(McpHttpServerTest, ServerEndsTheSessionAClientDeletes)
{
    ServerUnderTest server;
    server.transport.start();
    ASSERT_TRUE(server.transport.isOpen());
    const QString session = server.transport.sessionId();

    EXPECT_EQ(server.request("DELETE", session).statusCode, 200);

    const QByteArray ping = compact(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/x"}});
    EXPECT_EQ(server.request("POST", session, ping).statusCode, 404)
        << "after termination the server MUST answer 404 to the ended session's id";
    EXPECT_EQ(server.request("DELETE", session).statusCode, 404);
    EXPECT_TRUE(server.transport.isOpen()) << "the server keeps listening for the next session";

    const HttpResponse fresh = server.request("POST", {}, ping);
    EXPECT_EQ(fresh.statusCode, 202);
    const QByteArray next = fresh.rawHeader(QByteArrayView("Mcp-Session-Id"));
    EXPECT_FALSE(next.isEmpty());
    EXPECT_NE(QString::fromUtf8(next), session) << "a new session must not reuse the ended id";
    EXPECT_EQ(server.request("POST", QString::fromUtf8(next), ping).statusCode, 202);
}

TEST_F(McpHttpServerTest, ServerRefusesToDeleteASessionItDoesNotKnow)
{
    ServerUnderTest server;
    server.transport.start();
    const QString session = server.transport.sessionId();

    EXPECT_EQ(server.request("DELETE", "someone-else").statusCode, 400);
    EXPECT_EQ(server.request("DELETE", {}).statusCode, 400);
    EXPECT_EQ(server.transport.sessionId(), session);

    const HttpResponse other = server.request("PUT", session);
    EXPECT_EQ(other.statusCode, 405);
    EXPECT_EQ(other.rawHeader(QByteArrayView("Allow")), QByteArray("POST, DELETE"));
}

TEST_F(McpHttpServerTest, ClientStopEndsTheSessionOnAnLlmqoreServer)
{
    HttpServerConfig serverCfg;
    serverCfg.address = QHostAddress::LocalHost;
    serverCfg.port = 0;
    serverCfg.path = "/mcp";
    auto *serverTransport = new McpHttpServerTransport(serverCfg);
    McpServer server(serverTransport, McpServerConfig{{"http-delete-server", "0.0.1"}});
    server.start();
    const QString session = serverTransport->sessionId();

    HttpTransportConfig clientCfg;
    clientCfg.endpoint
        = QUrl(QString("http://127.0.0.1:%1/mcp").arg(serverTransport->serverPort()));
    auto *clientTransport = new McpStreamableHttpTransport(clientCfg);
    McpClient client(clientTransport, Implementation{"http-delete-client", "0.0.1"});
    waitForFuture(client.connectAndInitialize(std::chrono::seconds(5)));
    ASSERT_EQ(clientTransport->sessionId(), session);

    QtWarningCapture capture;
    clientTransport->stop();
    for (int i = 0; i < 100 && serverTransport->sessionId() == session; ++i)
        pumpEventLoop(std::chrono::milliseconds(20));

    EXPECT_NE(serverTransport->sessionId(), session) << "the DELETE must reach the server";
    EXPECT_TRUE(capture.warnings().isEmpty()) << capture.warnings().join('\n').toStdString();

    server.stop();
    delete clientTransport;
    delete serverTransport;
}

#include "tst_McpHttpServer.moc"

