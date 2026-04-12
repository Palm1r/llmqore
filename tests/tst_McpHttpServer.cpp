// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <LLMCore/BaseTool.hpp>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpHttpServerTransport.hpp>
#include <LLMCore/McpHttpTransport.hpp>
#include <LLMCore/McpServer.hpp>

using namespace LLMCore;
using namespace LLMCore::Mcp;

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
    QFuture<LLMCore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        const QString text = input.value("text").toString();
        return QtConcurrent::run([text]() -> LLMCore::ToolResult {
            return LLMCore::ToolResult::text(QString("echo: %1").arg(text));
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
    auto *clientTransport = new McpHttpTransport(clientCfg);
    McpClient client(clientTransport, Implementation{"http-loopback-client", "0.0.1"});

    const InitializeResult init = waitForFuture(
        client.connectAndInitialize(std::chrono::seconds(5)));
    EXPECT_EQ(init.serverInfo.name, "http-loopback-server");
    EXPECT_TRUE(client.isInitialized());

    const QList<ToolInfo> tools = waitForFuture(client.listTools());
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools.first().name, "echo");

    const LLMCore::ToolResult result = waitForFuture(
        client.callTool("echo", QJsonObject{{"text", "over-http"}}));
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.asText(), "echo: over-http");

    client.shutdown();
    server.stop();

    delete clientTransport;
    delete serverTransport;
}

#include "tst_McpHttpServer.moc"
