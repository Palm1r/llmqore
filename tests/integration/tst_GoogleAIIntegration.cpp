// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"
#include <LLMQore/GoogleAIClient.hpp>

using namespace LLMQore;
using namespace LLMQore::IntegrationTest;

class GoogleAIIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_apiKey = getEnvOrSkip("GOOGLE_API_KEY");
        m_url
            = getEnvOrDefault("GOOGLE_API_URL", "https://generativelanguage.googleapis.com/v1beta");
        m_model = getEnvOrDefault("GOOGLE_MODEL", "gemini-3.1-flash-lite-preview");
    }

    std::unique_ptr<GoogleAIClient> createClient()
    {
        return std::make_unique<GoogleAIClient>(
            m_url, m_apiKey, m_model);
    }

    QString m_apiKey;
    QString m_url;
    QString m_model;
};

TEST_F(GoogleAIIntegrationTest, SimpleTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["contents"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"parts", QJsonArray{QJsonObject{{"text", "Reply with exactly: Hello Integration Test"}}}}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 0) << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, SimpleStringPrompt)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    client->ask("Reply with exactly one word: Pong");

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, StreamingChunks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["contents"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"parts",
         QJsonArray{QJsonObject{
             {"text", "Count from 1 to 30, one number per line, no other text."}}}}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 1) << "Expected multiple chunks\n" << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, ToolUse_EchoTool)
{
    auto client = createClient();
    auto *echoTool = new EchoTool(client.get());
    client->tools()->addTool(echoTool);

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["contents"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"parts",
         QJsonArray{QJsonObject{
             {"text", "Use the echo tool to echo 'google test works'. Then tell me the result."}}}}}};
    payload["tools"] = client->tools()->getToolsDefinitions();

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty()) << "Model did not use any tools\n"
                                             << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, ToolUse_ImageReturningTool)
{
    // Exercises the Gemini 3-series multimodal function-response path:
    // a tool returns a ToolResult containing an image content block;
    // GoogleMessage::createToolResultParts upgrades the wire shape to
    // emit `functionResponse.parts[].inlineData` for the image.
    //
    // Requires a Gemini 3 model (default GOOGLE_MODEL already is).
    auto client = createClient();
    auto *imageTool = new ImageReturningTool(client.get());
    client->tools()->addTool(imageTool);

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["contents"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"parts",
         QJsonArray{QJsonObject{
             {"text",
              "Call the get_sample_image tool (no arguments), then tell me what "
              "colour the returned image is. Reply with a single lowercase colour "
              "word like 'red' or 'blue'."}}}}}};
    payload["tools"] = client->tools()->getToolsDefinitions();

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty())
        << "Model did not invoke the image-returning tool\n" << result.diagnostics();
    // The key invariant is that the multi-turn loop survived a non-text
    // tool result. The exact colour detection is advisory only.
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, ToolUse_Calculator)
{
    auto client = createClient();
    auto *calcTool = new CalculatorTool(client.get());
    client->tools()->addTool(calcTool);

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["contents"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"parts",
         QJsonArray{QJsonObject{
             {"text", "Use the calculator to divide 100 by 4. Tell me the result."}}}}}};
    payload["tools"] = client->tools()->getToolsDefinitions();

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("25")) << "Expected 25 in response\n"
                                                << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, ThinkingBlocks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject thinkingConfig;
    thinkingConfig["thinkingBudget"] = 5000;

    QJsonObject generationConfig;
    generationConfig["thinkingConfig"] = thinkingConfig;

    QJsonObject payload;
    payload["contents"] = QJsonArray{QJsonObject{
        {"role", "user"}, {"parts", QJsonArray{QJsonObject{{"text", "What is 15 factorial?"}}}}}};
    payload["generationConfig"] = generationConfig;

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();

    if (!result.thinkingBlocks.isEmpty()) {
        EXPECT_FALSE(result.thinkingBlocks[0].first.isEmpty()) << "Thinking text empty\n"
                                                               << result.diagnostics();
    }
}

TEST_F(GoogleAIIntegrationTest, ImageMessage_InlineData)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    const QString base64Png = "iVBORw0KGgoAAAANSUhEUgAAAAoAAAAKCAIAAAACUFjqAAAAEklEQVR4nGP4"
                              "z8CAB+GTG8HSALfKY52fTcuYAAAAAElFTkSuQmCC";

    QJsonArray parts;
    parts.append(QJsonObject{{"text", "What color is this image? Reply one word."}});
    parts.append(
        QJsonObject{{"inlineData", QJsonObject{{"mimeType", "image/png"}, {"data", base64Png}}}});

    QJsonObject payload;
    payload["model"] = m_model;
    payload["contents"] = QJsonArray{QJsonObject{{"role", "user"}, {"parts", parts}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.toLower().contains("red")) << "Expected 'red' in response\n"
                                                           << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, BufferedTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["contents"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"parts", QJsonArray{QJsonObject{{"text", "Reply with exactly: Buffered OK"}}}}}};

    client->sendMessage(payload, {}, RequestMode::Buffered);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("Buffered")) << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, BufferedStringPrompt)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    client->ask("Reply with exactly one word: Pong", RequestMode::Buffered);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(GoogleAIIntegrationTest, ListModels)
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
    EXPECT_GT(models.size(), 0) << "No models returned from Google AI";
}
