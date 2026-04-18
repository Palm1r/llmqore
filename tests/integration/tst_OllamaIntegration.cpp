// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "IntegrationTestHelpers.hpp"
#include <LLMQore/OllamaClient.hpp>

#include <QTcpSocket>

using namespace LLMQore;
using namespace LLMQore::IntegrationTest;

constexpr int kOllamaTimeoutMs = 120000;

class OllamaIntegrationTest : public ProviderTestBase
{
protected:
    void SetUp() override
    {
        ProviderTestBase::SetUp();

        m_url = getEnvOrDefault("OLLAMA_URL", "http://localhost:11434");
        m_model = getEnvOrDefault("OLLAMA_MODEL", "qwen3.5:9b");

        QTcpSocket socket;
        QUrl url(m_url);
        socket.connectToHost(url.host(), static_cast<quint16>(url.port(11434)));
        if (!socket.waitForConnected(3000)) {
            GTEST_SKIP() << "Ollama is not reachable at " << m_url.toStdString();
        }
        socket.disconnectFromHost();
    }

    std::unique_ptr<OllamaClient> createClient()
    {
        return std::make_unique<OllamaClient>(
            m_url, QString(), m_model);
    }

    QString m_url;
    QString m_model;
};

TEST_F(OllamaIntegrationTest, SimpleTextResponse)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "Reply with exactly: Hello Integration Test"}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 0) << result.diagnostics();
}

TEST_F(OllamaIntegrationTest, SimpleStringPrompt)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    client->ask("Reply with exactly one word: Pong");

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(OllamaIntegrationTest, StreamingChunks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["stream"] = true;
    // Ask for a long enough response that the API cannot coalesce it into
    // a single chunk — counting to 30 gives ~90 characters, which always
    // streams as multiple chunks.
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content", "Count from 1 to 30, one number per line, no other text."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_GT(result.chunks.size(), 1) << "Expected multiple chunks\n" << result.diagnostics();
}

TEST_F(OllamaIntegrationTest, ToolUse_EchoTool)
{
    auto client = createClient();
    auto *echoTool = new EchoTool(client.get());
    client->tools()->addTool(echoTool);

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content", "Use the echo tool to echo 'ollama test works'. Then tell me the result."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
}

TEST_F(OllamaIntegrationTest, ToolUse_Calculator)
{
    auto client = createClient();
    auto *calcTool = new CalculatorTool(client.get());
    client->tools()->addTool(calcTool);

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["stream"] = true;
    payload["tools"] = client->tools()->getToolsDefinitions();
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"}, {"content", "Use the calculator to add 15 and 27. Tell me the result."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("42")) << "Expected 42 in response\n"
                                                << result.diagnostics();
}

TEST_F(OllamaIntegrationTest, ThinkingBlocks)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    QJsonObject payload;
    payload["model"] = m_model;
    payload["stream"] = true;
    payload["enable_thinking"] = true;
    payload["messages"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"content", "What is 2 + 3? Think before answering."}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();

    if (!result.thinkingBlocks.isEmpty()) {
        EXPECT_FALSE(result.thinkingBlocks[0].first.isEmpty()) << "Thinking text empty\n"
                                                               << result.diagnostics();
    }
}

TEST_F(OllamaIntegrationTest, ImageMessage_Base64)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    // 64x64 solid red PNG (must be >= 32x32 for vision model image processors)
    const QString base64Png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAIAAAAlC+aJAAAAb0lEQVR4nO3P"
                              "AQkAAAyEwO9feoshgnABdLep8QUNyPEFDcjxBQ3I8QUNyPEFDcjxBQ3I8QUN"
                              "yPEFDcjxBQ3I8QUNyPEFDcjxBQ3I8QUNyPEFDcjxBQ3I8QUNyPEFDcjxBQ3I"
                              "8QUNyPEFDcjxBQ3IPanc8OLDQitxAAAAAElFTkSuQmCC";

    QJsonObject payload;
    payload["model"] = m_model;
    payload["stream"] = true;
    payload["messages"] = QJsonArray{QJsonObject{
        {"role", "user"},
        {"content", "What color is this image? Reply one word."},
        {"images", QJsonArray{base64Png}}}};

    client->sendMessage(payload);

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(OllamaIntegrationTest, BufferedTextResponse)
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

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.failed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
    EXPECT_TRUE(result.fullText.contains("Buffered")) << result.diagnostics();
}

TEST_F(OllamaIntegrationTest, BufferedStringPrompt)
{
    auto client = createClient();

    TestResult result;
    QEventLoop loop;
    wireLoggingSignals(client.get(), result, loop);

    client->ask("Reply with exactly one word: Pong", RequestMode::Buffered);

    waitWithTimeout(loop, result, kOllamaTimeoutMs);

    ASSERT_FALSE(result.timedOut) << "Request timed out\n" << result.diagnostics();
    ASSERT_TRUE(result.completed) << result.diagnostics();
    EXPECT_FALSE(result.fullText.isEmpty()) << result.diagnostics();
}

TEST_F(OllamaIntegrationTest, ListModels)
{
    auto client = createClient();

    auto future = client->listModels();

    QEventLoop loop;
    QFutureWatcher<QList<QString>> watcher;
    QObject::connect(&watcher, &QFutureWatcher<QList<QString>>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(kRequestTimeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    ASSERT_TRUE(future.isFinished()) << "ListModels request timed out";
    QList<QString> models = future.result();
    EXPECT_GT(models.size(), 0) << "No models returned from Ollama";
}
