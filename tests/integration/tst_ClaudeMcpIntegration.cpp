// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

// End-to-end loopback: a real ClaudeClient talks to a local McpServer via
// McpPipeTransport, exercising the full MCP handshake + tools/list + tools/call
// path through the BaseClient -> ToolsManager -> McpRemoteTool adapter chain.

#include "IntegrationTestHelpers.hpp"

#include <QEventLoop>
#include <QFutureWatcher>
#include <QTimer>

#include <LLMCore/ClaudeClient.hpp>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpPipeTransport.hpp>
#include <LLMCore/McpServer.hpp>
#include <LLMCore/McpTypes.hpp>
#include <LLMCore/ToolsManager.hpp>

using namespace LLMCore;
using namespace LLMCore::Mcp;
using namespace LLMCore::IntegrationTest;

namespace {

template<typename T>
void waitForFuture(const QFuture<T> &future, int timeoutMs = 5000)
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

} // namespace

class ClaudeMcpIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_apiKey = getEnvOrSkip("CLAUDE_API_KEY");
        m_url = getEnvOrDefault("CLAUDE_API_URL", "https://api.anthropic.com");
        m_model = getEnvOrDefault("CLAUDE_MODEL", "claude-sonnet-4-6");
    }

    struct McpFixture
    {
        std::unique_ptr<McpServer> server;
        std::unique_ptr<McpClient> client;
    };

    McpFixture startMcpServerWithTools(ToolsManager *manager)
    {
        auto [serverTransport, clientTransport] = McpPipeTransport::createPair();

        McpServerConfig cfg;
        cfg.serverInfo = {"llmcore-integration-server", "0.0.1"};
        auto server = std::make_unique<McpServer>(serverTransport, cfg);
        server->addTool(new EchoTool(server.get()));
        server->addTool(new CalculatorTool(server.get()));

        auto client = std::make_unique<McpClient>(
            clientTransport, Implementation{"llmcore-integration-client", "0.0.1"});

        server->start();
        waitForFuture(client->connectAndInitialize());

        manager->addMcpClient(client.get());

        // Allow the async listTools to complete.
        QEventLoop loop;
        QTimer::singleShot(1000, &loop, &QEventLoop::quit);
        loop.exec();

        return McpFixture{std::move(server), std::move(client)};
    }

    std::unique_ptr<ClaudeClient> createClient()
    {
        return std::make_unique<ClaudeClient>(m_url, m_apiKey, m_model);
    }

    QString m_apiKey;
    QString m_url;
    QString m_model;
};

TEST_F(ClaudeMcpIntegrationTest, ListsToolsFromMcpServerIntoToolsManager)
{
    auto claude = createClient();
    auto fixture = startMcpServerWithTools(claude->tools());

    // The ToolsManager should have both tools registered.
    EXPECT_EQ(claude->tools()->registeredTools().size(), 2);
    EXPECT_NE(claude->tools()->tool("echo"), nullptr);
    EXPECT_NE(claude->tools()->tool("calculator"), nullptr);

    // getToolsDefinitions() must succeed and emit a schema array Claude accepts.
    const QJsonArray defs = claude->tools()->getToolsDefinitions();
    EXPECT_EQ(defs.size(), 2);
}

TEST_F(ClaudeMcpIntegrationTest, ClaudeCallsMcpEchoTool)
{
    auto claude = createClient();
    auto fixture = startMcpServerWithTools(claude->tools());

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 500;
    payload["stream"] = true;
    payload["tools"] = claude->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content",
         "Use the echo tool to echo exactly the message 'mcp loopback ok'. "
         "Then tell me what the tool returned."}}};

    claude->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty())
        << "Model did not invoke the MCP-backed echo tool\n" << result.diagnostics();

    // Check that at least one observed tool call mentions the echo tool.
    bool sawEcho = false;
    for (const auto &[name, payload] : result.toolCalls) {
        if (name.contains("echo"))
            sawEcho = true;
    }
    EXPECT_TRUE(sawEcho) << "No echo tool call observed\n" << result.diagnostics();
}

TEST_F(ClaudeMcpIntegrationTest, ClaudeCallsMcpCalculatorTool)
{
    auto claude = createClient();
    auto fixture = startMcpServerWithTools(claude->tools());

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 500;
    payload["stream"] = true;
    payload["tools"] = claude->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content",
         "Use the calculator tool to multiply 13 by 17. Report the numeric result."}}};

    claude->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("221"))
        << "Expected 13*17=221 in the final answer\n" << result.diagnostics();
}
