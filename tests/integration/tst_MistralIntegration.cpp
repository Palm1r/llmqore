// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"
#include <LLMQore/MistralClient.hpp>

using namespace LLMQore;
using namespace LLMQore::IntegrationTest;

class MistralIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_apiKey = getEnvOrSkip("MISTRAL_API_KEY");
        m_url = getEnvOrDefault("MISTRAL_API_URL", "https://api.mistral.ai/v1");
        m_model = getEnvOrDefault("MISTRAL_MODEL", "mistral-small-latest");
        m_fimModel = getEnvOrDefault("MISTRAL_FIM_MODEL", "codestral-latest");
    }

    std::unique_ptr<MistralClient> createClient()
    {
        return std::make_unique<MistralClient>(m_url, m_apiKey, m_model);
    }

    std::unique_ptr<MistralClient> createFimClient()
    {
        return std::make_unique<MistralClient>(m_url, m_apiKey, m_fimModel);
    }

    QString m_apiKey;
    QString m_url;
    QString m_model;
    QString m_fimModel;
};

TEST_F(MistralIntegrationTest, SimpleTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 50;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: Hello Integration Test"}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 0) << result.diagnostics();
}

TEST_F(MistralIntegrationTest, SimpleStringPrompt)
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

TEST_F(MistralIntegrationTest, StreamingChunks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 500;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content", "Count from 1 to 30, one number per line, no other text."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 1) << "Expected multiple chunks\n" << result.diagnostics();
}

TEST_F(MistralIntegrationTest, ToolUse_EchoTool)
{
    auto client = createClient();
    auto *echoTool = new EchoTool(client.get());
    client->tools()->addTool(echoTool);

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 300;
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content",
         "Use the echo tool to echo 'integration test works'. Then tell me the result."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty()) << "Model did not use any tools\n"
                                             << result.diagnostics();
}

TEST_F(MistralIntegrationTest, ToolUse_Calculator)
{
    auto client = createClient();
    auto *calcTool = new CalculatorTool(client.get());
    client->tools()->addTool(calcTool);

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 300;
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content", "Use the calculator to multiply 7 by 8. Tell me the result."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("56")) << "Expected 56 in response\n"
                                                << result.diagnostics();
}

TEST_F(MistralIntegrationTest, ImageMessage_Base64)
{
    // Mistral vision models (mistral-small-latest, pixtral-*) accept the
    // OpenAI-compatible `image_url` content-block shape.
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonArray content;
    content.append(
        QJsonObject{{"type", "text"}, {"text", "What color is this image? Reply one word."}});
    content.append(
        QJsonObject{
            {"type", "image_url"},
            {"image_url",
             QJsonObject{{"url", QString("data:image/png;base64,%1").arg(kTinyPngBase64)}}}});

    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 500;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", content}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(MistralIntegrationTest, BufferedTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["stream"] = false;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: Buffered OK"}}};

    client->sendMessage(payload, {}, RequestMode::Buffered);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("Buffered")) << result.diagnostics();
}

TEST_F(MistralIntegrationTest, BufferedStringPrompt)
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

TEST_F(MistralIntegrationTest, ListModels)
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

// --- Mistral-specific: Fill-In-the-Middle (FIM) completion ---
//
// FIM is only available on Codestral models and lives behind the
// /fim/completions endpoint, which the caller selects explicitly via
// the endpoint parameter on sendMessage().

constexpr const char *kFimEndpoint = "/fim/completions";

TEST_F(MistralIntegrationTest, FimCompletion_Streaming)
{
    auto client = createFimClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_fimModel;
    payload["prompt"] = "def fib(n):\n    ";
    payload["suffix"] = "\n\nprint(fib(10))\n";
    payload["max_tokens"] = 64;

    client->sendMessage(payload, kFimEndpoint);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(MistralIntegrationTest, FimCompletion_NoSuffix)
{
    // `suffix` is optional in the FIM API; the explicit endpoint
    // argument is what selects the route, not the payload shape.
    auto client = createFimClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_fimModel;
    payload["prompt"] = "// Returns the factorial of n\nint factorial(int n) {\n    ";
    payload["max_tokens"] = 64;

    client->sendMessage(payload, kFimEndpoint);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(MistralIntegrationTest, FimCompletion_Buffered)
{
    auto client = createFimClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_fimModel;
    payload["prompt"] = "def add(a, b):\n    return ";
    payload["suffix"] = "\n";
    payload["max_tokens"] = 32;

    client->sendMessage(payload, kFimEndpoint, RequestMode::Buffered);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}
