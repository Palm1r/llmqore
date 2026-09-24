// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"
#include <LLMQore/Conversation.hpp>
#include <LLMQore/OpenAIClient.hpp>

using namespace LLMQore;
using namespace LLMQore::IntegrationTest;

class QwenIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_apiKey = getEnvOrSkip("QWEN_API_KEY");
        m_url = getEnvOrDefault(
            "QWEN_API_URL", "https://dashscope-intl.aliyuncs.com/compatible-mode/v1");
        m_model = getEnvOrDefault("QWEN_MODEL", "qwen3.7-flash");
    }

    std::unique_ptr<OpenAIClient> createClient()
    {
        return std::make_unique<OpenAIClient>(m_url, m_apiKey, m_model);
    }

    QString m_apiKey;
    QString m_url;
    QString m_model;
};

TEST_F(QwenIntegrationTest, SimpleTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: Hello Integration Test"}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kRequestTimeoutMs);
    LLMQORE_SKIP_IF_RATE_LIMITED(result);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(QwenIntegrationTest, StreamingChunks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Count from 1 to 5, digits only."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kRequestTimeoutMs);
    LLMQORE_SKIP_IF_RATE_LIMITED(result);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 0) << result.diagnostics();
}

TEST_F(QwenIntegrationTest, BufferedTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: buffered"}}};

    client->sendMessage(payload, {}, RequestMode::Buffered);

    waitWithTimeout(loop, result, kRequestTimeoutMs);
    LLMQORE_SKIP_IF_RATE_LIMITED(result);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_LE(result.chunks.size(), 1) << "Buffered mode delivers the answer in one piece\n"
                                       << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(QwenIntegrationTest, ToolUse_Calculator)
{
    auto client = createClient();
    client->tools()->addTool(new CalculatorTool);

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    Conversation conversation;
    conversation.addUser("Use the calculator tool to compute 2+2.");

    client->ask(conversation);

    waitWithTimeout(loop, result, kToolContinuationTimeoutMs);
    LLMQORE_SKIP_IF_RATE_LIMITED(result);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.toolCalls.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("4")) << result.diagnostics();
}

TEST_F(QwenIntegrationTest, ListModels)
{
    auto client = createClient();

    auto future = client->listModels();

    QEventLoop loop;
    QFutureWatcher<QList<ModelInfo>> watcher;
    QObject::connect(&watcher, &QFutureWatcher<QList<ModelInfo>>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(kRequestTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    ASSERT_TRUE(future.isFinished()) << "ListModels timed out";
    QList<ModelInfo> models = future.result();
    EXPECT_GT(models.size(), 0) << "No models returned";
}

TEST_F(QwenIntegrationTest, ConversationMultiTurn)
{
    auto client = createClient();
    expectMultiTurnAccepted(client.get());
}

TEST_F(QwenIntegrationTest, AskOnceResolves)
{
    auto client = createClient();
    expectAskOnceResolves(client.get());
}
