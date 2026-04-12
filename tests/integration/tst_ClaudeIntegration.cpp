// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"
#include <LLMCore/ClaudeClient.hpp>

using namespace LLMCore;
using namespace LLMCore::IntegrationTest;

class ClaudeIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_apiKey = getEnvOrSkip("CLAUDE_API_KEY");
        m_url = getEnvOrDefault("CLAUDE_API_URL", "https://api.anthropic.com");
        m_model = getEnvOrDefault("CLAUDE_MODEL", "claude-sonnet-4-6");
    }

    std::unique_ptr<ClaudeClient> createClient()
    {
        return std::make_unique<ClaudeClient>(
            m_url, m_apiKey, m_model);
    }

    QString m_apiKey;
    QString m_url;
    QString m_model;
};

TEST_F(ClaudeIntegrationTest, SimpleTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 250;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: Hello Integration Test"}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("Hello")) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 0) << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, SimpleStringPrompt)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    client->ask("Reply with exactly one word: Pong", callbacks);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, StreamingChunks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 800;
    payload["stream"] = true;
    // Ask for a response long enough to cross Claude's internal batching
    // threshold — short bodies (a few hundred bytes) can be coalesced into
    // a single content_block_delta event on fast paths, so we request an
    // explicit 500-word essay to guarantee multiple text deltas.
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content",
         "Write a 500-word essay about the history of programming languages, "
         "covering FORTRAN, C, and Python. Use complete sentences."}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 1) << "Expected multiple chunks\n" << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, ToolUse_EchoTool)
{
    auto client = createClient();
    auto *echoTool = new EchoTool(client.get());
    client->tools()->addTool(echoTool);

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 500;
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content",
         "Use the echo tool to echo the message 'integration test works'. "
         "Then tell me the result."}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty()) << "Model did not use any tools\n"
                                             << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, ToolUse_Calculator)
{
    auto client = createClient();
    auto *calcTool = new CalculatorTool(client.get());
    client->tools()->addTool(calcTool);

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 500;
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content", "Use the calculator tool to multiply 7 by 8. Tell me the result."}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("56")) << "Expected 56 in response\n"
                                                << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, ExtendedThinking)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 8000;
    payload["stream"] = true;
    payload["thinking"] = QJsonObject{{"type", "enabled"}, {"budget_tokens", 5000}};
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "What is 15 factorial? Think step by step."}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.thinkingBlocks.isEmpty()) << "No thinking blocks\n" << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, ImageMessage_Base64)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    const QString base64Png = "iVBORw0KGgoAAAANSUhEUgAAAAoAAAAKCAIAAAACUFjqAAAAEklEQVR4nGP4"
                              "z8CAB+GTG8HSALfKY52fTcuYAAAAAElFTkSuQmCC";

    QJsonObject imageSource;
    imageSource["type"] = "base64";
    imageSource["media_type"] = "image/png";
    imageSource["data"] = base64Png;

    QJsonArray content;
    content.append(
        QJsonObject{{"type", "text"}, {"text", "What color is this image? Reply with one word."}});
    content.append(QJsonObject{{"type", "image"}, {"source", imageSource}});

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 2000;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.toLower().contains("red")) << "Expected 'red' in response\n"
                                                           << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, BufferedTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 250;
    payload["stream"] = false;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: Buffered OK"}}};

    client->sendMessage(payload, callbacks, RequestMode::Buffered);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("Buffered")) << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, BufferedStringPrompt)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    client->ask("Reply with exactly one word: Pong", callbacks, RequestMode::Buffered);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(ClaudeIntegrationTest, ListModels)
{
    auto client = createClient();

    auto future = client->listModels();

    QEventLoop loop;
    QFutureWatcher<QList<QString>> watcher;
    QObject::connect(&watcher, &QFutureWatcher<QList<QString>>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(kRequestTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    ASSERT_TRUE(future.isFinished()) << "ListModels timed out";
    QList<QString> models = future.result();
    EXPECT_GT(models.size(), 0) << "No models returned";
}
