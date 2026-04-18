// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"
#include <LLMQore/LlamaCppClient.hpp>

using namespace LLMQore;
using namespace LLMQore::IntegrationTest;

class LlamaCppIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_url = getEnvOrDefault("LLAMACPP_API_URL", "http://localhost:8080");
        m_apiKey = getEnvOrDefault("LLAMACPP_API_KEY", "");
        m_model = getEnvOrDefault("LLAMACPP_MODEL", "");

        auto client = createClient();
        auto future = client->isServerReady();

        QEventLoop loop;
        QFutureWatcher<bool> watcher;
        QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        loop.exec();

        if (!future.isFinished() || !future.result())
            GTEST_SKIP() << "llama-server not reachable at " << m_url.toStdString();
    }

    std::unique_ptr<LlamaCppClient> createClient()
    {
        return std::make_unique<LlamaCppClient>(
            m_url, m_apiKey, m_model);
    }

    QString m_apiKey;
    QString m_url;
    QString m_model;
};

TEST_F(LlamaCppIntegrationTest, IsServerReady)
{
    auto client = createClient();

    auto future = client->isServerReady();

    QEventLoop loop;
    QFutureWatcher<bool> watcher;
    QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kRequestTimeoutMs, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();

    ASSERT_TRUE(future.isFinished()) << "isServerReady timed out";
    EXPECT_TRUE(future.result());
}

TEST_F(LlamaCppIntegrationTest, SimpleTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    if (!m_model.isEmpty())
        payload["model"] = m_model;
    payload["max_tokens"] = 2048;
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

TEST_F(LlamaCppIntegrationTest, SimpleStringPrompt)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    if (!m_model.isEmpty())
        payload["model"] = m_model;
    payload["max_tokens"] = 2048;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly one word: Pong"}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(LlamaCppIntegrationTest, StreamingChunks)
{
    auto client = createClient();
    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    if (!m_model.isEmpty())
        payload["model"] = m_model;
    payload["max_tokens"] = 2048;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "What is 2+2? Answer with just the number."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("4")) << "Expected '4' in response\n"
                                               << result.diagnostics();
}

TEST_F(LlamaCppIntegrationTest, ThinkingModel_ReasoningContent)
{
    auto client = createClient();
    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    if (!m_model.isEmpty())
        payload["model"] = m_model;
    payload["max_tokens"] = 2048;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Explain why sky is blue in one sentence."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    // Non-thinking models will have 0 thinking blocks — that's fine, skip check
    if (!result.thinkingBlocks.isEmpty()) {
        EXPECT_FALSE(result.thinkingBlocks[0].first.trimmed().isEmpty())
            << "Thinking block should not be empty\n"
            << result.diagnostics();
    }
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(LlamaCppIntegrationTest, InfillRequest)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["input_prefix"] = "def hello():\n    print(\"hell";
    payload["input_suffix"] = "\")\n\ndef goodbye():\n    print(\"bye\")\n";
    payload["n_predict"] = 2048;

    client->sendMessage(payload, "/infill");

    waitWithTimeout(loop, result, kRequestTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(LlamaCppIntegrationTest, BufferedResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    if (!m_model.isEmpty())
        payload["model"] = m_model;
    payload["max_tokens"] = 2048;
    payload["stream"] = false;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: Buffered OK"}}};

    client->sendMessage(payload, {}, RequestMode::Buffered);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(LlamaCppIntegrationTest, ListModels)
{
    auto client = createClient();

    auto future = client->listModels();

    QEventLoop loop;
    QFutureWatcher<QList<QString>> watcher;
    QObject::connect(&watcher, &QFutureWatcher<QList<QString>>::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kRequestTimeoutMs, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();

    ASSERT_TRUE(future.isFinished()) << "ListModels timed out";
    QList<QString> models = future.result();
    EXPECT_GT(models.size(), 0) << "No models returned";
}

TEST_F(LlamaCppIntegrationTest, ServerProps)
{
    auto client = createClient();

    auto future = client->serverProps();

    QEventLoop loop;
    QFutureWatcher<QJsonObject> watcher;
    QObject::connect(&watcher, &QFutureWatcher<QJsonObject>::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kRequestTimeoutMs, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();

    ASSERT_TRUE(future.isFinished()) << "serverProps timed out";
    QJsonObject props = future.result();
    EXPECT_FALSE(props.isEmpty()) << "Empty props response";
    EXPECT_TRUE(props.contains("total_slots"));
    EXPECT_TRUE(props.contains("default_generation_settings"));
}
