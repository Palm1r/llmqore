// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/LlamaCppClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>

using namespace LLMQore;

namespace {
const int kRegisterMetaTypes = []() {
    qRegisterMetaType<TokenUsage>("LLMQore::TokenUsage");
    qRegisterMetaType<CompletionInfo>("LLMQore::CompletionInfo");
    qRegisterMetaType<RequestID>("LLMQore::RequestID");
    return 0;
}();
} // namespace

TEST(TokenUsage, DefaultIsInvalid)
{
    TokenUsage u;
    EXPECT_EQ(u.promptTokens, 0);
    EXPECT_EQ(u.completionTokens, 0);
    EXPECT_EQ(u.cachedPromptTokens, 0);
    EXPECT_EQ(u.reasoningTokens, 0);
    EXPECT_FALSE(u.isValid());
    EXPECT_EQ(u.totalTokens(), 0);
}

TEST(TokenUsage, ValidWithEitherPromptOrCompletion)
{
    TokenUsage onlyPrompt;
    onlyPrompt.promptTokens = 10;
    EXPECT_TRUE(onlyPrompt.isValid());

    TokenUsage onlyCompletion;
    onlyCompletion.completionTokens = 5;
    EXPECT_TRUE(onlyCompletion.isValid());
}

TEST(TokenUsage, TotalTokensSumsPromptAndCompletion)
{
    TokenUsage u;
    u.promptTokens = 100;
    u.completionTokens = 50;
    u.cachedPromptTokens = 25;
    u.reasoningTokens = 10;
    EXPECT_EQ(u.totalTokens(), 150);
}

namespace {

template<typename ClientT>
class UsageDriver : public ClientT
{
public:
    using ClientT::ClientT;
    using BaseClient::completeRequest;
    using BaseClient::createRequest;
    using BaseClient::currentUsage;
    using ClientT::processBufferedResponse;
    using ClientT::processData;
};

template<typename ClientT, typename ActionT>
CompletionInfo runAndCapture(UsageDriver<ClientT> &c, const RequestID &id, ActionT &&action)
{
    QSignalSpy spy(&c, &BaseClient::requestFinalized);
    action();
    if (spy.count() == 0)
        c.completeRequest(id);
    EXPECT_EQ(spy.count(), 1);
    if (spy.isEmpty())
        return {};
    const auto args = spy.takeFirst();
    return args.at(1).value<CompletionInfo>();
}

} // namespace

TEST(TokenUsageClaude, BufferedExtractsAllFields)
{
    UsageDriver<ClaudeClient> client;
    const auto id = client.createRequest();

    const QByteArray body = R"({
        "content": [{"type":"text","text":"hi"}],
        "stop_reason": "end_turn",
        "usage": {
            "input_tokens": 1500,
            "output_tokens": 250,
            "cache_read_input_tokens": 800
        }
    })";

    const auto info = runAndCapture(client, id, [&] { client.processBufferedResponse(id, body); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 1500);
    EXPECT_EQ(info.usage->completionTokens, 250);
    EXPECT_EQ(info.usage->cachedPromptTokens, 800);
    EXPECT_EQ(info.usage->reasoningTokens, 0); // Claude has no separate reasoning field here
}

TEST(TokenUsageClaude, BufferedMissingUsageLeavesNullopt)
{
    UsageDriver<ClaudeClient> client;
    const auto id = client.createRequest();

    const QByteArray body = R"({
        "content": [{"type":"text","text":"hi"}],
        "stop_reason": "end_turn"
    })";

    const auto info = runAndCapture(client, id, [&] { client.processBufferedResponse(id, body); });
    EXPECT_FALSE(info.usage.has_value());
}

TEST(TokenUsageClaude, StreamingMessageDeltaUpdatesCumulativeOutputTokens)
{
    UsageDriver<ClaudeClient> client;
    const auto id = client.createRequest();

    const QByteArray sse =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"usage\":{\"input_tokens\":300,"
        "\"output_tokens\":1,\"cache_read_input_tokens\":50}}}\n\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\"}}\n\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\","
        "\"text\":\"hi\"}}\n\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
        "\"usage\":{\"output_tokens\":75}}\n\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n\n";

    const auto info = runAndCapture(client, id, [&] { client.processData(id, sse); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 300);
    EXPECT_EQ(info.usage->completionTokens, 75);
    EXPECT_EQ(info.usage->cachedPromptTokens, 50);
}

TEST(TokenUsageOpenAI, BufferedExtractsBasicAndDetails)
{
    UsageDriver<OpenAIClient> client;
    const auto id = client.createRequest();

    const QByteArray body = R"({
        "choices": [{"finish_reason":"stop","message":{"content":"hi"}}],
        "usage": {
            "prompt_tokens": 1200,
            "completion_tokens": 80,
            "prompt_tokens_details": {"cached_tokens": 600},
            "completion_tokens_details": {"reasoning_tokens": 30}
        }
    })";

    const auto info = runAndCapture(client, id, [&] { client.processBufferedResponse(id, body); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 1200);
    EXPECT_EQ(info.usage->completionTokens, 80);
    EXPECT_EQ(info.usage->cachedPromptTokens, 600);
    EXPECT_EQ(info.usage->reasoningTokens, 30);
}

TEST(TokenUsageOpenAI, StreamingFinalChunkWithEmptyChoicesCarriesUsage)
{
    UsageDriver<OpenAIClient> client;
    const auto id = client.createRequest();

    const QByteArray sse =
        "data: {\"choices\":[{\"finish_reason\":null,\"delta\":{\"content\":\"hi\"}}]}\n\n"
        "data: {\"choices\":[{\"finish_reason\":\"stop\",\"delta\":{}}]}\n\n"
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":42,\"completion_tokens\":7}}\n\n"
        "data: [DONE]\n\n";

    const auto info = runAndCapture(client, id, [&] { client.processData(id, sse); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 42);
    EXPECT_EQ(info.usage->completionTokens, 7);
}

TEST(TokenUsageOpenAIResponses, BufferedExtractsDetailedUsage)
{
    UsageDriver<OpenAIResponsesClient> client;
    const auto id = client.createRequest();

    const QByteArray body = R"({
        "output": [],
        "status": "completed",
        "usage": {
            "input_tokens": 2000,
            "output_tokens": 400,
            "input_tokens_details": {"cached_tokens": 1000},
            "output_tokens_details": {"reasoning_tokens": 150}
        }
    })";

    const auto info = runAndCapture(client, id, [&] { client.processBufferedResponse(id, body); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 2000);
    EXPECT_EQ(info.usage->completionTokens, 400);
    EXPECT_EQ(info.usage->cachedPromptTokens, 1000);
    EXPECT_EQ(info.usage->reasoningTokens, 150);
}

TEST(TokenUsageOpenAIResponses, StreamingResponseCompletedCarriesUsage)
{
    UsageDriver<OpenAIResponsesClient> client;
    const auto id = client.createRequest();

    const QByteArray sse =
        "event: response.output_text.delta\n"
        "data: {\"delta\":\"hi\"}\n\n"
        "event: response.completed\n"
        "data: {\"response\":{\"status\":\"completed\",\"usage\":"
        "{\"input_tokens\":120,\"output_tokens\":30,"
        "\"output_tokens_details\":{\"reasoning_tokens\":8}}}}\n\n";

    const auto info = runAndCapture(client, id, [&] { client.processData(id, sse); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 120);
    EXPECT_EQ(info.usage->completionTokens, 30);
    EXPECT_EQ(info.usage->reasoningTokens, 8);
}

TEST(TokenUsageOllama, FinalDoneLineCarriesEvalCounts)
{
    UsageDriver<OllamaClient> client;
    const auto id = client.createRequest();

    const QByteArray ndjson =
        "{\"message\":{\"content\":\"hi\"},\"done\":false}\n"
        "{\"done\":true,\"prompt_eval_count\":256,\"eval_count\":64}\n";

    const auto info = runAndCapture(client, id, [&] { client.processData(id, ndjson); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 256);
    EXPECT_EQ(info.usage->completionTokens, 64);
}

TEST(TokenUsageOllama, BufferedDelegatesToSameExtraction)
{
    UsageDriver<OllamaClient> client;
    const auto id = client.createRequest();

    const QByteArray body =
        R"({"message":{"content":"hi"},"done":true,"prompt_eval_count":12,"eval_count":3})";

    const auto info = runAndCapture(client, id, [&] { client.processBufferedResponse(id, body); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 12);
    EXPECT_EQ(info.usage->completionTokens, 3);
}

TEST(TokenUsageLlamaCpp, NativeCompletionEndpointUsesTokensEvaluatedPredicted)
{
    UsageDriver<LlamaCppClient> client;
    const auto id = client.createRequest();

    const QByteArray body =
        R"({"content":"hi","stop":true,"tokens_evaluated":88,"tokens_predicted":22})";

    const auto info = runAndCapture(client, id, [&] { client.processBufferedResponse(id, body); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 88);
    EXPECT_EQ(info.usage->completionTokens, 22);
}

TEST(TokenUsageLlamaCpp, OpenAICompatPathUsesPromptCompletionTokens)
{
    UsageDriver<LlamaCppClient> client;
    const auto id = client.createRequest();

    const QByteArray body = R"({
        "choices": [{"finish_reason":"stop","message":{"content":"hi"}}],
        "usage": {"prompt_tokens": 33, "completion_tokens": 11}
    })";

    const auto info = runAndCapture(client, id, [&] { client.processBufferedResponse(id, body); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 33);
    EXPECT_EQ(info.usage->completionTokens, 11);
}

TEST(TokenUsageGoogleAI, UsageMetadataExtractsAllFields)
{
    UsageDriver<GoogleAIClient> client;
    const auto id = client.createRequest();

    const QByteArray body = R"({
        "candidates": [{
            "content": {"parts": [{"text": "hi"}]},
            "finishReason": "STOP"
        }],
        "usageMetadata": {
            "promptTokenCount": 500,
            "candidatesTokenCount": 120,
            "cachedContentTokenCount": 200,
            "thoughtsTokenCount": 40
        }
    })";

    const auto info = runAndCapture(client, id, [&] { client.processBufferedResponse(id, body); });

    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 500);
    EXPECT_EQ(info.usage->completionTokens, 120);
    EXPECT_EQ(info.usage->cachedPromptTokens, 200);
    EXPECT_EQ(info.usage->reasoningTokens, 40);
}
