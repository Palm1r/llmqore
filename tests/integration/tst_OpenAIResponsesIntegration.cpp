// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"
#include <LLMCore/OpenAIResponsesClient.hpp>

using namespace LLMCore;
using namespace LLMCore::IntegrationTest;

class OpenAIResponsesIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_apiKey = getEnvOrSkip("OPENAI_API_KEY");
        m_url = getEnvOrDefault("OPENAI_API_URL", "https://api.openai.com");
        m_model = getEnvOrDefault("OPENAI_RESPONSES_MODEL", "gpt-4.1-nano");
    }

    std::unique_ptr<OpenAIResponsesClient> createClient()
    {
        return std::make_unique<OpenAIResponsesClient>(
            m_url, m_apiKey, m_model);
    }

    QString m_apiKey;
    QString m_url;
    QString m_model;
};

TEST_F(OpenAIResponsesIntegrationTest, SimpleTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["input"] = "Reply with exactly: Hello Integration Test";
    payload["stream"] = true;

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 0) << result.diagnostics();
}

TEST_F(OpenAIResponsesIntegrationTest, SimpleStringPrompt)
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

TEST_F(OpenAIResponsesIntegrationTest, StreamingChunks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["input"] = "Count from 1 to 5, one number per line.";
    payload["stream"] = true;

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 1) << "Expected multiple chunks\n" << result.diagnostics();
}

TEST_F(OpenAIResponsesIntegrationTest, ToolUse_EchoTool)
{
    auto client = createClient();
    auto *echoTool = new EchoTool(client.get());
    client->tools()->addTool(echoTool);

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["input"]
        = "Use the echo tool to echo 'responses integration test'. Then tell me the result.";
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty()) << "Model did not use any tools\n"
                                             << result.diagnostics();
}

TEST_F(OpenAIResponsesIntegrationTest, ToolUse_Calculator)
{
    auto client = createClient();
    auto *calcTool = new CalculatorTool(client.get());
    client->tools()->addTool(calcTool);

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["input"] = "Use the calculator to add 123 and 456. Tell me the result.";
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("579")) << "Expected 579 in response\n"
                                                 << result.diagnostics();
}

TEST_F(OpenAIResponsesIntegrationTest, ImageMessage_InputImage)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    const QString base64Png = "iVBORw0KGgoAAAANSUhEUgAAAAoAAAAKCAIAAAACUFjqAAAAEklEQVR4nGP4"
                              "z8CAB+GTG8HSALfKY52fTcuYAAAAAElFTkSuQmCC";

    QJsonArray inputContent;
    inputContent.append(
        QJsonObject{{"type", "input_text"}, {"text", "What color is this image? Reply one word."}});
    inputContent.append(
        QJsonObject{
            {"type", "input_image"},
            {"image_url", QString("data:image/png;base64,%1").arg(base64Png)},
            {"detail", "high"}});

    QJsonArray input;
    input.append(QJsonObject{{"role", "user"}, {"content", inputContent}});

    QJsonObject payload;
    payload["model"] = "gpt-5.4";
    payload["input"] = input;
    payload["stream"] = true;

    client->sendMessage(payload, callbacks);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.toLower().contains("red")) << "Expected 'red' in response\n"
                                                           << result.diagnostics();
}

TEST_F(OpenAIResponsesIntegrationTest, BufferedTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    auto callbacks = makeLoggingCallbacks(result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["input"] = "Reply with exactly: Buffered OK";
    payload["stream"] = false;

    client->sendMessage(payload, callbacks, RequestMode::Buffered);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("Buffered")) << result.diagnostics();
}

TEST_F(OpenAIResponsesIntegrationTest, BufferedStringPrompt)
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

TEST_F(OpenAIResponsesIntegrationTest, ListModels)
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
