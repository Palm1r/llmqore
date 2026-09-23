// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QPromise>
#include <QSignalSpy>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpStream.hpp>
#include <LLMQore/LlamaCppClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "clients/ollama/OllamaMessage.hpp"

#include "FakeHttpTransport.hpp"

using namespace LLMQore;
using LLMQoreTest::FakeHttpTransport;

namespace {

const int kRegisterRegressionMetaTypes = []() {
    qRegisterMetaType<TokenUsage>("LLMQore::TokenUsage");
    qRegisterMetaType<CompletionInfo>("LLMQore::CompletionInfo");
    qRegisterMetaType<RequestID>("LLMQore::RequestID");
    return 0;
}();

CompletionInfo finalizedInfo(const QSignalSpy &spy, int index = 0)
{
    return spy.at(index).at(1).value<CompletionInfo>();
}

class EchoTool : public BaseTool
{
    Q_OBJECT
public:
    explicit EchoTool(QString reply, QObject *parent = nullptr)
        : BaseTool(parent)
        , m_reply(std::move(reply))
    {}

    QString id() const override { return QStringLiteral("echo"); }
    QString displayName() const override { return QStringLiteral("Echo"); }
    QString description() const override { return QStringLiteral("Echoes a value"); }
    QJsonObject parametersSchema() const override { return QJsonObject{{"type", "object"}}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        QPromise<ToolResult> promise;
        QFuture<ToolResult> future = promise.future();
        promise.start();
        promise.addResult(ToolResult::text(m_reply));
        promise.finish();
        return future;
    }

private:
    QString m_reply;
};

} // namespace

// --- 1. stopReason reached only 4 of 7 providers ---

TEST(ReviewRegression, GoogleCarriesStopReasonIntoCompletionInfo)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"hello\"}]},"
        "\"finishReason\":\"STOP\"}]}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    EXPECT_EQ(finalizedInfo(finalized).stopReason, QStringLiteral("STOP"))
        << "GoogleAIClient::onStreamFinished must carry the message stop reason";
}

TEST(ReviewRegression, OllamaCarriesDoneReasonIntoCompletionInfo)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", "", "llama-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "{\"message\":{\"content\":\"hello\"},\"done\":false}\n"
        "{\"done\":true,\"done_reason\":\"stop\"}\n");

    ASSERT_EQ(finalized.count(), 1);
    EXPECT_EQ(finalizedInfo(finalized).stopReason, QStringLiteral("stop"));
}

TEST(ReviewRegression, OpenAIResponsesCarriesStatusIntoCompletionInfo)
{
    FakeHttpTransport transport;
    OpenAIResponsesClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: response.output_text.delta\n"
        "data: {\"delta\":\"hello\"}\n\n"
        "event: response.completed\n"
        "data: {\"response\":{\"status\":\"completed\"}}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    EXPECT_EQ(finalizedInfo(finalized).stopReason, QStringLiteral("completed"));
}

// --- 2. HttpStream (and the QNetworkReply it adopts) had no owner ---

TEST(ReviewRegression, OpenedStreamIsOwnedByTheHttpClient)
{
    auto client = std::make_unique<HttpClient>();
    QNetworkRequest request(QUrl("http://127.0.0.1:1/never"));

    QPointer<HttpStreamHandle> stream = client->openStream(request, QByteArrayView("GET"));
    ASSERT_FALSE(stream.isNull());

    client.reset();
    EXPECT_TRUE(stream.isNull())
        << "a stream the caller never took leaks itself and the QNetworkReply it adopted";
}

TEST(ReviewRegression, StreamDeletedByTheCallerSurvivesClientTeardown)
{
    auto client = std::make_unique<HttpClient>();
    QNetworkRequest request(QUrl("http://127.0.0.1:1/never"));

    QPointer<HttpStreamHandle> stream = client->openStream(request, QByteArrayView("GET"));
    ASSERT_FALSE(stream.isNull());

    delete stream.data();
    ASSERT_TRUE(stream.isNull());

    client.reset();
    SUCCEED() << "the client must not double-delete a stream the caller already took";
}

// --- 3. tryParseToolCall left m_currentThinkingContent dangling ---

TEST(ReviewRegression, OllamaThinkingAfterToolCallDoesNotReuseFreedBlock)
{
    OllamaMessage msg;

    msg.handleThinkingDelta(QStringLiteral("first thought"));
    ASSERT_EQ(msg.currentThinkingContent().size(), 1);

    msg.handleContentDelta(R"({"name":"echo","arguments":{"value":"7"}})");
    msg.handleStopReason(true);
    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_TRUE(msg.currentThinkingContent().isEmpty())
        << "the tool-call path deletes every accumulated block";

    msg.handleThinkingDelta(QStringLiteral("second thought"));

    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent().front().thinking,
              QStringLiteral("second thought"))
        << "a stale m_currentThinkingContent would append into freed memory";
}

// --- 4. setResponseContent bypassed the accumulated-content signal ---

TEST(ReviewRegression, ResponsesAggregatedTextStillEmitsAccumulated)
{
    FakeHttpTransport transport;
    OpenAIResponsesClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy accumulated(&client, &BaseClient::accumulatedReceived);
    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: response.completed\n"
        "data: {\"response\":{\"status\":\"completed\","
        "\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\","
        "\"text\":\"aggregated answer\"}]}]}}\n\n");

    ASSERT_EQ(completed.count(), 1);
    EXPECT_EQ(completed.first().at(1).toString(), QStringLiteral("aggregated answer"));

    ASSERT_FALSE(accumulated.isEmpty())
        << "writing the accumulator directly leaves the UI without an update";
    EXPECT_EQ(accumulated.last().at(1).toString(), QStringLiteral("aggregated answer"));
}

// --- 5. Google sniffed raw JSON per chunk, and recorded failures for unknown ids ---

TEST(ReviewRegression, GoogleErrorBodySplitAcrossChunksStillFailsTheRequest)
{
    const QByteArray body = QJsonDocument(QJsonObject{
                                {"error",
                                 QJsonObject{{"code", 429}, {"message", "Quota exceeded"}}}})
                                .toJson(QJsonDocument::Compact);

    for (int split = 1; split < body.size(); ++split) {
        FakeHttpTransport transport;
        GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

        QSignalSpy failed(&client, &BaseClient::requestFailed);

        client.ask(QStringLiteral("hi"));
        ASSERT_EQ(transport.streamCount(), 1) << "split at byte " << split;

        auto *stream = transport.lastStream();
        stream->sendHeaders(200);
        stream->sendChunk(body.left(split));
        stream->sendChunk(body.mid(split));
        stream->sendFinished();

        ASSERT_EQ(failed.count(), 1) << "split at byte " << split;
        EXPECT_TRUE(failed.first().at(1).toString().contains(QStringLiteral("Quota exceeded")))
            << "split at byte " << split;
    }
}

TEST(ReviewRegression, GoogleErrorAfterCancelIsNotReportedAgain)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    const RequestID id = client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    auto *stream = transport.lastStream();
    stream->sendHeaders(200);

    client.cancelRequest(id);

    QSignalSpy failed(&client, &BaseClient::requestFailed);
    stream->sendChunk(R"({"error":{"code":429,"message":"Quota exceeded"}})");
    stream->sendFinished();

    EXPECT_EQ(failed.count(), 0)
        << "a cancelled request must not be recorded as failed after the fact";
}

TEST(ReviewRegression, GoogleSseStreamIsUnaffectedByTheErrorSniffer)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    auto *stream = transport.lastStream();
    stream->sendHeaders(200);
    stream->sendChunk("data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"he");
    stream->sendChunk("llo\"}]},\"finishReason\":\"STOP\"}]}\n\n");
    stream->sendFinished();

    ASSERT_EQ(completed.count(), 1);
    EXPECT_EQ(completed.first().at(1).toString(), QStringLiteral("hello"));
}

// --- listModels is one helper in the base, driven per provider ---

namespace {

QStringList resolveModels(FakeHttpTransport &transport, QFuture<QList<ModelInfo>> future,
                             int statusCode, const QByteArray &body)
{
    transport.respondToLast(statusCode, body);
    for (int i = 0; i < 16 && !future.isFinished(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    QStringList ids;
    if (!future.isFinished())
        return ids;
    for (const ModelInfo &info : future.result())
        ids.append(info.id);
    return ids;
}

} // namespace

TEST(ListModels, OpenAIReadsDataIdPairs)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    auto future = client.listModels();
    ASSERT_EQ(transport.bufferedCount(), 1);
    EXPECT_EQ(transport.bufferedRequest(0).verb, QByteArray("GET"));
    EXPECT_EQ(transport.bufferedRequest(0).url(), QUrl("http://fake.local/v1/models"));

    const auto models = resolveModels(
        transport, future, 200, R"({"data":[{"id":"gpt-a"},{"id":"gpt-b"},{"noid":1}]})");
    EXPECT_EQ(models, (QStringList{"gpt-a", "gpt-b"}));
}

TEST(ListModels, OllamaReadsModelsNamePairs)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", "", "llama-test", &transport);

    auto future = client.listModels();
    ASSERT_EQ(transport.bufferedCount(), 1);
    EXPECT_EQ(transport.bufferedRequest(0).url(), QUrl("http://fake.local/api/tags"));

    const auto models = resolveModels(
        transport, future, 200, R"({"models":[{"name":"llama3"},{"name":"qwen"}]})");
    EXPECT_EQ(models, (QStringList{"llama3", "qwen"}));
}

TEST(ListModels, GoogleStripsThePublisherPrefix)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    auto future = client.listModels();
    ASSERT_EQ(transport.bufferedCount(), 1);

    const auto models = resolveModels(
        transport, future, 200,
        R"({"models":[{"name":"models/gemini-2.0"},{"name":"bare-name"}]})");
    EXPECT_EQ(models, (QStringList{"gemini-2.0", "bare-name"}));
}

TEST(ListModels, ClaudeAsksForTheFullPage)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    auto future = client.listModels();
    ASSERT_EQ(transport.bufferedCount(), 1);
    EXPECT_EQ(transport.bufferedRequest(0).url().query(), QStringLiteral("limit=1000"));

    const auto models = resolveModels(transport, future, 200, R"({"data":[{"id":"claude-x"}]})");
    EXPECT_EQ(models, (QStringList{"claude-x"}));
}

TEST(ListModels, MistralKeepsTheV1Prefix)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local", "sk-test", "mistral-test", &transport);
    client.setProfile(mistralProfile());

    auto future = client.listModels();
    ASSERT_EQ(transport.bufferedCount(), 1);
    EXPECT_EQ(transport.bufferedRequest(0).url(), QUrl("http://fake.local/v1/models"));

    const auto models = resolveModels(transport, future, 200, R"({"data":[{"id":"mistral-a"}]})");
    EXPECT_EQ(models, (QStringList{"mistral-a"}));
}

TEST(ListModels, LlamaCppKeepsTheV1Prefix)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-test", &transport);

    auto future = client.listModels();
    ASSERT_EQ(transport.bufferedCount(), 1);
    EXPECT_EQ(transport.bufferedRequest(0).url(), QUrl("http://fake.local/v1/models"));

    const auto models = resolveModels(transport, future, 200, R"({"data":[{"id":"qwen.gguf"}]})");
    EXPECT_EQ(models, (QStringList{"qwen.gguf"}));
}

TEST(ListModels, OpenAIResponsesReadsDataIdPairs)
{
    FakeHttpTransport transport;
    OpenAIResponsesClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    auto future = client.listModels();
    ASSERT_EQ(transport.bufferedCount(), 1);
    EXPECT_EQ(transport.bufferedRequest(0).url(), QUrl("http://fake.local/v1/models"));

    const auto models = resolveModels(transport, future, 200, R"({"data":[{"id":"gpt-5"}]})");
    EXPECT_EQ(models, (QStringList{"gpt-5"}));
}

TEST(ListModels, HttpErrorYieldsAnEmptyList)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    auto future = client.listModels();
    const auto models = resolveModels(transport, future, 500, R"({"error":{"message":"boom"}})");
    EXPECT_TRUE(models.isEmpty());
}

TEST(ListModels, CustomEndpointOverridesTheProviderDefault)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    auto future = client.listModels(QStringLiteral("/custom/models"));
    ASSERT_EQ(transport.bufferedCount(), 1);
    EXPECT_EQ(transport.bufferedRequest(0).url(), QUrl("http://fake.local/v1/custom/models"));

    const auto models = resolveModels(transport, future, 200, R"({"data":[{"id":"m"}]})");
    EXPECT_EQ(models, (QStringList{"m"}));
}

// --- parseHttpError is one shared shape with per-provider annotations ---

TEST(ParseHttpError, ClaudeRendersTheTypeBare)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    QSignalSpy failed(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"));
    auto *stream = transport.lastStream();
    stream->sendHeaders(429);
    stream->sendChunk(R"({"error":{"message":"slow down","type":"rate_limit_error"}})");
    stream->sendFinished();

    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed.first().at(1).toString(),
              QStringLiteral("HTTP 429: slow down (rate_limit_error)"));
}

TEST(ParseHttpError, GoogleLabelsCodeAndStatus)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);

    QSignalSpy failed(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"));
    auto *stream = transport.lastStream();
    stream->sendHeaders(403);
    stream->sendChunk(
        R"({"error":{"message":"denied","code":403,"status":"PERMISSION_DENIED"}})");
    stream->sendFinished();

    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed.first().at(1).toString(),
              QStringLiteral("HTTP 403: denied (code: 403) (status: PERMISSION_DENIED)"));
}

TEST(ParseHttpError, OpenAILabelsTypeAndCodeAndSkipsMissingOnes)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy failed(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"));
    auto *stream = transport.lastStream();
    stream->sendHeaders(400);
    stream->sendChunk(R"({"error":{"message":"bad request","type":"invalid_request_error"}})");
    stream->sendFinished();

    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed.first().at(1).toString(),
              QStringLiteral("HTTP 400: bad request (type: invalid_request_error)"));
}

TEST(ParseHttpError, OllamaReadsTheBareErrorString)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", "", "llama-test", &transport);

    QSignalSpy failed(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"));
    auto *stream = transport.lastStream();
    stream->sendHeaders(404);
    stream->sendChunk(R"({"error":"model 'llama-test' not found"})");
    stream->sendFinished();

    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed.first().at(1).toString(),
              QStringLiteral("HTTP 404: model 'llama-test' not found"));
}

TEST(ParseHttpError, OllamaWithoutAnErrorFieldFallsBackToTheSnippet)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", "", "llama-test", &transport);

    QSignalSpy failed(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"));
    auto *stream = transport.lastStream();
    stream->sendHeaders(500);
    stream->sendChunk(R"({"detail":"boom"})");
    stream->sendFinished();

    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed.first().at(1).toString(), QStringLiteral(R"(HTTP 500: {"detail":"boom"})"));
}

TEST(ParseHttpError, BodyWithoutAnErrorObjectFallsBackToTheSnippet)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy failed(&client, &BaseClient::requestFailed);

    client.ask(QStringLiteral("hi"));
    auto *stream = transport.lastStream();
    stream->sendHeaders(502);
    stream->sendChunk("<html>bad gateway</html>");
    stream->sendFinished();

    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed.first().at(1).toString(),
              QStringLiteral("HTTP 502: <html>bad gateway</html>"));
}

// --- LlamaCpp now inherits the OpenAI dialect instead of copying it ---

TEST(LlamaCppInheritance, RunsTheOpenAIToolLoop)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);
    client.tools()->addTool(new EchoTool(QStringLiteral("42")));

    QSignalSpy toolStarted(&client, &BaseClient::toolStarted);
    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("call echo"));
    ASSERT_EQ(transport.streamCount(), 1);
    EXPECT_EQ(transport.streamRequest(0).url().path(), QStringLiteral("/v1/chat/completions"));

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
        "\"function\":{\"name\":\"echo\",\"arguments\":\"{}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2)) << "no continuation was sent";
    ASSERT_EQ(toolStarted.count(), 1);
    EXPECT_EQ(toolStarted.first().at(2).toString(), QStringLiteral("echo"));

    const QJsonArray messages = transport.streamRequest(1).payload().value("messages").toArray();
    ASSERT_EQ(messages.size(), 3);
    EXPECT_EQ(messages.at(2).toObject().value("role").toString(), QStringLiteral("tool"));

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"content\":\"42\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(completed.count(), 1);
    EXPECT_EQ(completed.first().at(1).toString(), QStringLiteral("42"));
}

TEST(LlamaCppInheritance, NativeCompletionShapeStillEndsTheRequest)
{
    FakeHttpTransport transport;
    LlamaCppClient client("http://fake.local", "", "local-model", &transport);

    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    client.sendMessage(QJsonObject{{"prompt", "hi"}}, QStringLiteral("/completion"));
    ASSERT_EQ(transport.streamCount(), 1);
    EXPECT_EQ(transport.streamRequest(0).url().path(), QStringLiteral("/completion"));

    transport.lastStream()->sendAll(
        "data: {\"content\":\"one \",\"stop\":false}\n\n"
        "data: {\"content\":\"two\",\"stop\":true,\"tokens_evaluated\":5,"
        "\"tokens_predicted\":2}\n\n"
        "data: {\"content\":\"never\",\"stop\":false}\n\n");

    ASSERT_EQ(completed.count(), 1) << "events after stop must not restart the request";
    EXPECT_EQ(completed.first().at(1).toString(), QStringLiteral("one two"));
}

// --- 6. tool round-trips were dropped before the app could keep them ---

TEST(ReviewRegression, CompletionInfoCarriesTheToolRoundTrip)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);
    client.tools()->addTool(new EchoTool(QStringLiteral("42")));

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("what is six times seven?"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"echo\",\"input\":{}}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"value\\\":\\\"7\\\"}\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));

    transport.lastStream()->sendAll(
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"text_delta\",\"text\":\"42\"}}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    const QJsonArray messages
        = finalizedInfo(finalized).requestPayload.value("messages").toArray();

    ASSERT_EQ(messages.size(), 3)
        << "the assistant tool_use turn and the tool_result turn must survive the loop";
    EXPECT_EQ(messages.at(0).toObject().value("role").toString(), QStringLiteral("user"));
    EXPECT_EQ(messages.at(1).toObject().value("role").toString(), QStringLiteral("assistant"));

    const QJsonArray toolContent = messages.at(2).toObject().value("content").toArray();
    ASSERT_EQ(toolContent.size(), 1);
    EXPECT_EQ(toolContent.first().toObject().value("tool_use_id").toString(),
              QStringLiteral("toolu_1"));
}

TEST(ReviewRegression, CompletionInfoCarriesThePayloadOfAToolFreeTurn)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"text\",\"text\":\"\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"text_delta\",\"text\":\"hello\"}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    const QJsonArray messages
        = finalizedInfo(finalized).requestPayload.value("messages").toArray();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().toObject().value("role").toString(), QStringLiteral("user"));
}


// --- T8: one flush hook, one thinking-emission mechanism ---

TEST(StreamFlush, TrailingSseEventWithoutABlankLineIsStillDispatched)
{
    struct Case
    {
        const char *name;
        std::function<std::unique_ptr<BaseClient>(FakeHttpTransport &)> make;
        QByteArray tail;
        QString expected;
    };

    const std::vector<Case> cases{
        {"OpenAI",
         [](FakeHttpTransport &t) {
             return std::make_unique<OpenAIClient>("http://fake.local/v1", "k", "m", &t);
         },
         "data: {\"choices\":[{\"delta\":{\"content\":\"tail\"}}]}\n",
         QStringLiteral("tail")},
        {"Claude",
         [](FakeHttpTransport &t) {
             return std::make_unique<ClaudeClient>("http://fake.local", "k", "m", &t);
         },
         "event: content_block_delta\n"
         "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
         "{\"type\":\"text_delta\",\"text\":\"tail\"}}\n",
         QStringLiteral("tail")},
        {"Google",
         [](FakeHttpTransport &t) {
             return std::make_unique<GoogleAIClient>("http://fake.local", "k", "m", &t);
         },
         "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"tail\"}]}}]}\n",
         QStringLiteral("tail")},
        {"OpenAIResponses",
         [](FakeHttpTransport &t) {
             return std::make_unique<OpenAIResponsesClient>("http://fake.local/v1", "k", "m", &t);
         },
         "event: response.output_text.delta\n"
         "data: {\"delta\":\"tail\"}\n",
         QStringLiteral("tail")},
    };

    for (const Case &c : cases) {
        FakeHttpTransport transport;
        auto client = c.make(transport);

        QSignalSpy completed(client.get(), &BaseClient::requestCompleted);

        client->ask(QStringLiteral("hi"));
        ASSERT_EQ(transport.streamCount(), 1) << c.name;

        auto *stream = transport.lastStream();
        stream->sendHeaders(200);
        if (QString(c.name) == QLatin1String("Claude")) {
            stream->sendChunk(
                "event: message_start\n"
                "data: {\"type\":\"message_start\",\"message\":{}}\n\n"
                "event: content_block_start\n"
                "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
                "{\"type\":\"text\",\"text\":\"\"}}\n\n");
        }
        // No trailing blank line: the event sits in the parser until the flush.
        stream->sendChunk(c.tail);
        stream->sendFinished();

        ASSERT_EQ(completed.count(), 1) << c.name;
        EXPECT_EQ(completed.first().at(1).toString(), c.expected)
            << c.name << " lost its trailing event at end of stream";
    }
}

TEST(ThinkingEmission, ClaudeRedactedAndPlainBlocksGoThroughTheSamePath)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    QSignalSpy thinking(&client, &BaseClient::thinkingBlockReceived);

    client.ask(QStringLiteral("hi"));
    transport.lastStream()->sendAll(
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"thinking\",\"thinking\":\"\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"thinking_delta\",\"thinking\":\"pondering\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"signature_delta\",\"signature\":\"sig-1\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":"
        "{\"type\":\"redacted_thinking\",\"data\":\"sig-2\"}}\n\n"
        "event: content_block_stop\n"
        "data: {\"type\":\"content_block_stop\",\"index\":1}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n");

    ASSERT_EQ(thinking.count(), 2) << "both thinking shapes must be announced exactly once";
    EXPECT_EQ(thinking.at(0).at(1).toString(), QStringLiteral("pondering"));
    EXPECT_EQ(thinking.at(0).at(2).toString(), QStringLiteral("sig-1"));
    EXPECT_TRUE(thinking.at(1).at(1).toString().isEmpty()) << "a redacted block carries no text";
}

TEST(ThinkingEmission, ABlockIsAnnouncedOnlyOnceAcrossRepeatedNotifications)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    QSignalSpy thinking(&client, &BaseClient::thinkingBlockReceived);

    client.ask(QStringLiteral("hi"));
    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"one \"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"two\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(thinking.count(), 1) << "the reasoning block must not be re-announced per delta";
    EXPECT_EQ(thinking.first().at(1).toString(), QStringLiteral("one two"));
}

// --- T12: defects found while verifying T1-T9 ---

TEST(ReviewRegression, HttpClientForgetsStreamsTheCallerAlreadyTook)
{
    auto client = std::make_unique<HttpClient>();
    QNetworkRequest request(QUrl("http://127.0.0.1:1/never"));

    for (int i = 0; i < 8; ++i) {
        QPointer<HttpStreamHandle> taken = client->openStream(request, QByteArrayView("GET"));
        ASSERT_FALSE(taken.isNull());
        delete taken.data();
        ASSERT_TRUE(taken.isNull());
    }

    QPointer<HttpStreamHandle> live = client->openStream(request, QByteArrayView("GET"));
    ASSERT_FALSE(live.isNull());

    client.reset();
    EXPECT_TRUE(live.isNull())
        << "pruning the dead entries must not lose the one stream still owned by the client";
}

TEST(ReviewRegression, GoogleCompletionInfoCarriesItsOwnConversationKey)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);
    client.tools()->addTool(new EchoTool(QStringLiteral("42")));

    QSignalSpy finalized(&client, &BaseClient::requestFinalized);

    client.ask(QStringLiteral("what is six times seven?"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"candidates\":[{\"content\":{\"parts\":[{\"functionCall\":"
        "{\"name\":\"echo\",\"args\":{\"value\":\"7\"}}}]},\"finishReason\":\"STOP\"}]}\n\n");

    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));

    transport.lastStream()->sendAll(
        "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"42\"}]},"
        "\"finishReason\":\"STOP\"}]}\n\n");

    ASSERT_EQ(finalized.count(), 1);
    const QJsonObject payload = finalizedInfo(finalized).requestPayload;

    EXPECT_FALSE(payload.contains("messages"))
        << "a host that reads `messages` back gets nothing from Gemini";

    const QJsonArray contents = payload.value("contents").toArray();
    ASSERT_EQ(contents.size(), 3)
        << "the model turn and the function response must survive the loop";
    EXPECT_EQ(contents.at(0).toObject().value("role").toString(), QStringLiteral("user"));
    EXPECT_EQ(contents.at(1).toObject().value("role").toString(), QStringLiteral("model"));
    EXPECT_EQ(contents.at(2).toObject().value("role").toString(), QStringLiteral("function"));
}

TEST(ListModels, TransportFailureYieldsAnEmptyList)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    auto future = client.listModels();
    ASSERT_EQ(transport.bufferedCount(), 1);

    transport.failLast(QStringLiteral("connection refused"));
    for (int i = 0; i < 16 && !future.isFinished(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    ASSERT_TRUE(future.isFinished());
    EXPECT_TRUE(future.result().isEmpty());
}

namespace {

template<typename T>
class CategoryProbe : public T
{
public:
    using T::T;
    using T::logCategory;
};

} // namespace

TEST(LogCategory, EveryProviderReportsUnderItsOwnName)
{
    CategoryProbe<ClaudeClient> claude;
    CategoryProbe<OpenAIClient> openai;
    CategoryProbe<OllamaClient> ollama;
    CategoryProbe<GoogleAIClient> google;

    EXPECT_STREQ(claude.logCategory().categoryName(), "llmqore.claude");
    EXPECT_STREQ(openai.logCategory().categoryName(), "llmqore.openai");
    EXPECT_STREQ(ollama.logCategory().categoryName(), "llmqore.ollama");
    EXPECT_STREQ(google.logCategory().categoryName(), "llmqore.google");
}

TEST(LogCategory, OpenAIDerivedClientsKeepTheirOwnNameOnEveryConstructor)
{
    CategoryProbe<LlamaCppClient> llamaPlain;
    EXPECT_STREQ(llamaPlain.logCategory().categoryName(), "llmqore.llamacpp");

    CategoryProbe<LlamaCppClient> llama("http://fake.local", "", "m");
    EXPECT_STREQ(llama.logCategory().categoryName(), "llmqore.llamacpp")
        << "the transport-less constructor must not fall through to the OpenAI category";
}

TEST(LogCategory, AProfileCarriesItsOwnCategory)
{
    CategoryProbe<OpenAIClient> mistral;
    mistral.setProfile(mistralProfile());

    EXPECT_STREQ(mistral.logCategory().categoryName(), "llmqore.mistral")
        << "a profile is the whole of what MistralClient used to be";
}

#include "tst_ReviewRegressions.moc"

TEST(MistralProfile, StreamsTextAndReasoningThroughTheOpenAIDialect)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local", "sk-test", "magistral-small", &transport);
    client.setProfile(mistralProfile());

    QSignalSpy chunks(&client, &BaseClient::chunkReceived);
    QSignalSpy thinking(&client, &BaseClient::thinkingBlockReceived);
    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("why is the sky blue?"));
    ASSERT_EQ(transport.streamCount(), 1);
    EXPECT_EQ(transport.streamRequest(0).url(), QUrl("http://fake.local/v1/chat/completions"))
        << "the profile carries Mistral's /v1 prefix";

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"Rayleigh scattering.\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"Because of scattering.\"}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_EQ(completed.count(), 1);
    EXPECT_EQ(completed.first().at(1).toString(), QStringLiteral("Because of scattering."));

    ASSERT_EQ(chunks.count(), 1) << "reasoning must not reach chunkReceived";
    EXPECT_EQ(chunks.first().at(1).toString(), QStringLiteral("Because of scattering."));

    ASSERT_EQ(thinking.count(), 1) << "Magistral reasoning arrives as a thinking block";
    EXPECT_EQ(thinking.first().at(1).toString(), QStringLiteral("Rayleigh scattering."));
}

TEST(MistralProfile, BufferedResponseTakesTheSamePath)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local", "sk-test", "mistral-small", &transport);
    client.setProfile(mistralProfile());

    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    client.ask(QStringLiteral("ping"), RequestMode::Buffered);
    ASSERT_EQ(transport.bufferedCount(), 1);
    transport.respondToLast(
        200,
        R"({"choices":[{"message":{"role":"assistant","content":"pong"},)"
        R"("finish_reason":"stop"}]})");

    for (int i = 0; i < 32 && completed.isEmpty(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    ASSERT_EQ(completed.count(), 1);
    EXPECT_EQ(completed.first().at(1).toString(), QStringLiteral("pong"));
}
