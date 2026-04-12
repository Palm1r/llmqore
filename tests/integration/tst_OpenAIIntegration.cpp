// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"
#include <LLMCore/OpenAIClient.hpp>

using namespace LLMCore;
using namespace LLMCore::IntegrationTest;

class OpenAIIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_apiKey = getEnvOrSkip("OPENAI_API_KEY");
        m_url = getEnvOrDefault("OPENAI_API_URL", "https://api.openai.com");
        m_model = getEnvOrDefault("OPENAI_MODEL", "gpt-4.1-nano");
    }

    std::unique_ptr<OpenAIClient> createClient()
    {
        return std::make_unique<OpenAIClient>(
            m_url, m_apiKey, m_model);
    }

    QString m_apiKey;
    QString m_url;
    QString m_model;
};

TEST_F(OpenAIIntegrationTest, SimpleTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 50;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: Hello Integration Test"}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 0) << result.diagnostics();
}

TEST_F(OpenAIIntegrationTest, SimpleStringPrompt)
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

TEST_F(OpenAIIntegrationTest, StreamingChunks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 500;
    payload["stream"] = true;
    // Ask for a long enough response that the API cannot coalesce it into
    // a single SSE chunk — counting to 30 gives ~90 characters, which the
    // API always streams as multiple chunks.
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content", "Count from 1 to 30, one number per line, no other text."}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 1) << "Expected multiple chunks\n" << result.diagnostics();
}

TEST_F(OpenAIIntegrationTest, ToolUse_EchoTool)
{
    auto client = createClient();
    auto *echoTool = new EchoTool(client.get());
    client->tools()->addTool(echoTool);

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 300;
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content",
         "Use the echo tool to echo 'integration test works'. Then tell me the result."}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty()) << "Model did not use any tools\n"
                                             << result.diagnostics();
}

TEST_F(OpenAIIntegrationTest, ToolUse_Calculator)
{
    auto client = createClient();
    auto *calcTool = new CalculatorTool(client.get());
    client->tools()->addTool(calcTool);

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 300;
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content", "Use the calculator to multiply 7 by 8. Tell me the result."}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("56")) << "Expected 56 in response\n"
                                                << result.diagnostics();
}

TEST_F(OpenAIIntegrationTest, ImageMessage_Base64)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    const QString base64Png = "iVBORw0KGgoAAAANSUhEUgAAAAoAAAAKCAIAAAACUFjqAAAAEklEQVR4nGP4"
                              "z8CAB+GTG8HSALfKY52fTcuYAAAAAElFTkSuQmCC";

    QJsonArray content;
    content.append(
        QJsonObject{{"type", "text"}, {"text", "What color is this image? Reply one word."}});
    content.append(
        QJsonObject{
            {"type", "image_url"},
            {"image_url",
             QJsonObject{{"url", QString("data:image/png;base64,%1").arg(base64Png)}}}});

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 500;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}};

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(OpenAIIntegrationTest, BufferedTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
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

TEST_F(OpenAIIntegrationTest, BufferedStringPrompt)
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

TEST_F(OpenAIIntegrationTest, ListModels)
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
