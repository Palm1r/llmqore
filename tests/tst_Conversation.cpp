// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>

#include "FakeHttpTransport.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/Conversation.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>

using namespace LLMQore;

namespace {

Conversation twoTurnConversation()
{
    Conversation conversation;
    conversation.setSystem("You are terse.");
    conversation.addUser("What is Qt?");
    conversation.addAssistant("A C++ framework.");
    conversation.addUser("And QML?");
    return conversation;
}

Conversation toolRoundConversation()
{
    Conversation conversation;
    conversation.addUser("Weather in Berlin?");
    conversation.addAssistant(
        QList<TurnContent>{ToolUseContent{"call_1", "get_weather", QJsonObject{{"city", "Berlin"}}}});
    conversation.addToolResults(
        {makeToolResultContent("call_1", "get_weather", ToolResult::text("22C sunny"))});
    return conversation;
}

} // namespace

TEST(Conversation, KeepsTurnOrderAndRoles)
{
    const Conversation conversation = twoTurnConversation();

    ASSERT_EQ(conversation.turns().size(), 3);
    EXPECT_EQ(conversation.turns()[0].role, TurnRole::User);
    EXPECT_EQ(conversation.turns()[1].role, TurnRole::Assistant);
    EXPECT_EQ(conversation.turns()[2].role, TurnRole::User);
    EXPECT_EQ(conversation.turns()[0].text(), "What is Qt?");
    EXPECT_EQ(conversation.system(), "You are terse.");
}

TEST(Conversation, JsonRoundTripPreservesEverything)
{
    Conversation original = toolRoundConversation();
    original.setSystem("system prompt");
    original.addUser(QList<TurnContent>{
        TextContent{"look"}, ImageContent::fromBytes(QByteArray("png"), "image/png")});

    const Conversation back = Conversation::fromJson(original.toJson());

    ASSERT_EQ(back.turns().size(), original.turns().size());
    EXPECT_EQ(back.system(), "system prompt");

    const auto &toolTurn = back.turns()[2];
    EXPECT_EQ(toolTurn.role, TurnRole::Tool);
    ASSERT_EQ(toolTurn.content.size(), 1);
    const auto *result = std::get_if<ToolResultContent>(&toolTurn.content.first());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->toolUseId, "call_1");
    EXPECT_EQ(toToolResult(*result).asText(), "22C sunny");

    const auto &imageTurn = back.turns().last();
    ASSERT_EQ(imageTurn.content.size(), 2);
    const auto *image = std::get_if<ImageContent>(&imageTurn.content[1]);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->bytes(), QByteArray("png"));
    EXPECT_EQ(image->mimeType, "image/png");
}

TEST(Conversation, ClaudePayloadUsesTopLevelSystem)
{
    ClaudeClient client("https://api.anthropic.com", "k", "claude-sonnet-4-5");
    const QJsonObject payload = client.buildConversationPayload(twoTurnConversation());

    EXPECT_EQ(payload.value("system").toString(), "You are terse.");
    EXPECT_TRUE(payload.contains("max_tokens"));

    const QJsonArray messages = payload.value("messages").toArray();
    ASSERT_EQ(messages.size(), 3);
    EXPECT_EQ(messages[0].toObject().value("role").toString(), "user");
    EXPECT_EQ(messages[1].toObject().value("role").toString(), "assistant");
}

TEST(Conversation, ClaudePutsToolResultsInAUserTurn)
{
    ClaudeClient client("https://api.anthropic.com", "k", "m");
    const QJsonObject payload = client.buildConversationPayload(toolRoundConversation());

    const QJsonArray messages = payload.value("messages").toArray();
    ASSERT_EQ(messages.size(), 3);

    const QJsonObject toolTurn = messages[2].toObject();
    EXPECT_EQ(toolTurn.value("role").toString(), "user");

    const QJsonObject block = toolTurn.value("content").toArray()[0].toObject();
    EXPECT_EQ(block.value("type").toString(), "tool_result");
    EXPECT_EQ(block.value("tool_use_id").toString(), "call_1");
}

TEST(Conversation, OpenAIPayloadUsesSystemMessageAndToolRole)
{
    OpenAIClient client("https://api.openai.com/v1", "k", "gpt-4o");
    const QJsonObject payload = client.buildConversationPayload(toolRoundConversation());

    const QJsonArray messages = payload.value("messages").toArray();
    ASSERT_EQ(messages.size(), 3);
    EXPECT_EQ(messages[1].toObject().value("role").toString(), "assistant");
    EXPECT_FALSE(messages[1].toObject().value("tool_calls").toArray().isEmpty());
    EXPECT_EQ(messages[2].toObject().value("role").toString(), "tool");
    EXPECT_EQ(messages[2].toObject().value("tool_call_id").toString(), "call_1");
}

TEST(Conversation, OpenAISendsImagesAsContentParts)
{
    Conversation conversation;
    conversation.addUser(QList<TurnContent>{
        TextContent{"describe"}, ImageContent::fromBytes(QByteArray("png"), "image/png")});

    OpenAIClient client("https://api.openai.com/v1", "k", "gpt-4o");
    const QJsonObject payload = client.buildConversationPayload(conversation);

    const QJsonArray parts
        = payload.value("messages").toArray()[0].toObject().value("content").toArray();
    ASSERT_EQ(parts.size(), 2);
    EXPECT_EQ(parts[0].toObject().value("type").toString(), "text");
    EXPECT_EQ(parts[1].toObject().value("type").toString(), "image_url");
    EXPECT_TRUE(parts[1]
                    .toObject()
                    .value("image_url")
                    .toObject()
                    .value("url")
                    .toString()
                    .startsWith("data:image/png;base64,"));
}

TEST(Conversation, GooglePayloadUsesContentsAndSystemInstruction)
{
    GoogleAIClient client("https://generativelanguage.googleapis.com/v1beta", "k", "gemini");
    const QJsonObject payload = client.buildConversationPayload(twoTurnConversation());

    EXPECT_EQ(
        payload.value("systemInstruction")
            .toObject()
            .value("parts")
            .toArray()[0]
            .toObject()
            .value("text")
            .toString(),
        "You are terse.");

    const QJsonArray contents = payload.value("contents").toArray();
    ASSERT_EQ(contents.size(), 3);
    EXPECT_EQ(contents[0].toObject().value("role").toString(), "user");
    EXPECT_EQ(contents[1].toObject().value("role").toString(), "model");
    EXPECT_EQ(
        contents[0].toObject().value("parts").toArray()[0].toObject().value("text").toString(),
        "What is Qt?");
}

TEST(Conversation, GoogleToolResultsUseFunctionRole)
{
    GoogleAIClient client("https://generativelanguage.googleapis.com/v1beta", "k", "gemini");
    const QJsonObject payload = client.buildConversationPayload(toolRoundConversation());

    const QJsonArray contents = payload.value("contents").toArray();
    ASSERT_EQ(contents.size(), 3);
    EXPECT_EQ(contents[2].toObject().value("role").toString(), "function");

    const QJsonObject response = contents[2]
                                     .toObject()
                                     .value("parts")
                                     .toArray()[0]
                                     .toObject()
                                     .value("functionResponse")
                                     .toObject();
    EXPECT_EQ(response.value("name").toString(), "get_weather");
}

TEST(Conversation, OllamaSendsImagesAlongsideContent)
{
    Conversation conversation;
    conversation.addUser(QList<TurnContent>{
        TextContent{"describe"}, ImageContent::fromBytes(QByteArray("png"), "image/png")});

    OllamaClient client("http://localhost:11434", {}, "llama3");
    const QJsonObject payload = client.buildConversationPayload(conversation);

    const QJsonObject message = payload.value("messages").toArray()[0].toObject();
    EXPECT_EQ(message.value("content").toString(), "describe");
    ASSERT_EQ(message.value("images").toArray().size(), 1);
}

TEST(Conversation, ResponsesUsesInstructionsAndFunctionCallOutput)
{
    OpenAIResponsesClient client("https://api.openai.com/v1", "k", "gpt-5");
    const QJsonObject payload = client.buildConversationPayload(toolRoundConversation());

    const QJsonArray input = payload.value("input").toArray();
    ASSERT_GE(input.size(), 3);
    EXPECT_EQ(input[1].toObject().value("type").toString(), "function_call");
    EXPECT_EQ(input[2].toObject().value("type").toString(), "function_call_output");
    EXPECT_EQ(input[2].toObject().value("call_id").toString(), "call_1");
}

TEST(Conversation, ResponsesReplaysReasoningOnlyWhenEnabled)
{
    Conversation conversation;
    ThinkingContent thinking;
    thinking.thinking = "step";
    thinking.itemId = "rs_1";
    thinking.encryptedContent = "cipher";
    conversation.addAssistant(QList<TurnContent>{thinking, TextContent{"answer"}});

    OpenAIResponsesClient off("https://api.openai.com/v1", "k", "gpt-5");
    const QJsonArray withoutReasoning
        = off.buildConversationPayload(conversation).value("input").toArray();
    for (const QJsonValue &item : withoutReasoning)
        EXPECT_NE(item.toObject().value("type").toString(), "reasoning");

    OpenAIResponsesClient on("https://api.openai.com/v1", "k", "gpt-5");
    on.setReasoningPersistence(OpenAIResponsesClient::ReasoningPersistence::Replay);
    const QJsonArray withReasoning = on.buildConversationPayload(conversation).value("input").toArray();

    ASSERT_FALSE(withReasoning.isEmpty());
    EXPECT_EQ(withReasoning[0].toObject().value("type").toString(), "reasoning");
    EXPECT_EQ(withReasoning[0].toObject().value("id").toString(), "rs_1");
    EXPECT_EQ(withReasoning[0].toObject().value("encrypted_content").toString(), "cipher");
}

TEST(AskOnce, ResolvesWithCompletionInfo)
{
    LLMQoreTest::FakeHttpTransport transport;
    ClaudeClient client("https://fake.local", "sk-test", "claude-test", &transport);

    auto future = client.askOnce("ping", RequestMode::Buffered);
    ASSERT_EQ(transport.bufferedCount(), 1);

    transport.respondToLast(
        200,
        R"({"content":[{"type":"text","text":"pong"}],"stop_reason":"end_turn",)"
        R"("usage":{"input_tokens":3,"output_tokens":1}})");

    for (int i = 0; i < 32 && !future.isFinished(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    ASSERT_TRUE(future.isFinished());
    const LLMQore::CompletionInfo info = future.result();
    EXPECT_EQ(info.fullText, "pong");
    EXPECT_EQ(info.stopReason, "end_turn");
    ASSERT_TRUE(info.usage.has_value());
    EXPECT_EQ(info.usage->promptTokens, 3);
}

TEST(AskOnce, RejectsOnHttpError)
{
    LLMQoreTest::FakeHttpTransport transport;
    ClaudeClient client("https://fake.local", "sk-test", "claude-test", &transport);

    auto future = client.askOnce("ping", RequestMode::Buffered);
    transport.respondToLast(401, R"({"error":{"type":"authentication_error"}})");

    for (int i = 0; i < 32 && !future.isFinished(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.result(), std::exception);
}

TEST(AskOnce, CarriesConversationForward)
{
    LLMQoreTest::FakeHttpTransport transport;
    ClaudeClient client("https://fake.local", "sk-test", "claude-test", &transport);

    Conversation conversation;
    conversation.addUser("What is Qt?");

    auto future = client.askOnce(conversation, {}, RequestMode::Buffered);
    transport.respondToLast(
        200, R"({"content":[{"type":"text","text":"A framework."}],"stop_reason":"end_turn"})");

    for (int i = 0; i < 32 && !future.isFinished(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    ASSERT_TRUE(future.isFinished());
    const Conversation back = future.result().conversation;
    ASSERT_EQ(back.turns().size(), 2);
    EXPECT_EQ(back.turns()[1].role, TurnRole::Assistant);
    EXPECT_EQ(back.turns()[1].text(), "A framework.");
}

TEST(AskOnce, CarriesAssistantBlocksNotJustText)
{
    LLMQoreTest::FakeHttpTransport transport;
    ClaudeClient client("https://fake.local", "sk-test", "claude-test", &transport);

    Conversation conversation;
    conversation.addUser("Think about Qt.");

    auto future = client.askOnce(conversation, {}, RequestMode::Buffered);
    transport.respondToLast(
        200,
        R"({"content":[{"type":"thinking","thinking":"pondering","signature":"sig-9"},)"
        R"({"type":"text","text":"A framework."}],"stop_reason":"end_turn"})");

    for (int i = 0; i < 32 && !future.isFinished(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    ASSERT_TRUE(future.isFinished());
    const Conversation back = future.result().conversation;
    ASSERT_EQ(back.turns().size(), 2);

    const QList<TurnContent> &blocks = back.turns()[1].content;
    ASSERT_EQ(blocks.size(), 2);

    const auto *thinking = std::get_if<ThinkingContent>(&blocks[0]);
    ASSERT_NE(thinking, nullptr);
    EXPECT_EQ(thinking->thinking, "pondering");
    EXPECT_EQ(thinking->signature, "sig-9");
    EXPECT_EQ(back.turns()[1].text(), "A framework.");
}

TEST(AskOnce, PromptRequestReturnsTheOneTurnConversationItBuilt)
{
    LLMQoreTest::FakeHttpTransport transport;
    ClaudeClient client("https://fake.local", "sk-test", "claude-test", &transport);

    auto future = client.askOnce("ping", RequestMode::Buffered);
    transport.respondToLast(
        200, R"({"content":[{"type":"text","text":"pong"}],"stop_reason":"end_turn"})");

    for (int i = 0; i < 32 && !future.isFinished(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    ASSERT_TRUE(future.isFinished());

    // A bare prompt is a one-turn conversation, so the round trip comes back
    // like any other: the caller can hand it straight to the next ask().
    const Conversation conversation = future.result().conversation;
    ASSERT_EQ(conversation.turns().size(), 2);
    EXPECT_EQ(conversation.turns()[0].role, TurnRole::User);
    EXPECT_EQ(conversation.turns()[0].text(), QStringLiteral("ping"));
    EXPECT_EQ(conversation.turns()[1].role, TurnRole::Assistant);
    EXPECT_EQ(conversation.turns()[1].text(), QStringLiteral("pong"));
}

TEST(AskOnce, RejectsWhenClientIsDestroyedMidFlight)
{
    LLMQoreTest::FakeHttpTransport transport;
    QFuture<CompletionInfo> future;

    {
        ClaudeClient client("https://fake.local", "sk-test", "claude-test", &transport);
        future = client.askOnce("ping", RequestMode::Buffered);
        ASSERT_FALSE(future.isFinished());
    }

    ASSERT_TRUE(future.isFinished());
    EXPECT_THROW(future.result(), std::exception);
}

namespace {

Conversation imageToolResultConversation()
{
    ToolResult result;
    result.content.append(TextContent{"Here is the sample image:"});
    result.content.append(ImageContent::fromBytes(QByteArray("PNGDATA"), "image/png"));

    Conversation conversation;
    conversation.addUser("show me");
    conversation.addAssistant(QList<TurnContent>{ToolUseContent{"c1", "get_image", {}}});
    conversation.addToolResults({makeToolResultContent("c1", "get_image", result)});
    return conversation;
}

} // namespace

TEST(Conversation, GoogleCarriesImageToolResultsAsInlineData)
{
    GoogleAIClient client("https://x", "k", "gemini");
    const QJsonObject turn = client.buildConversationPayload(imageToolResultConversation())
                                 .value("contents")
                                 .toArray()
                                 .last()
                                 .toObject();

    EXPECT_EQ(turn.value("role").toString(), "user");

    const QJsonArray parts = turn.value("parts").toArray();
    ASSERT_EQ(parts.size(), 1);

    const QJsonObject functionResponse = parts[0].toObject().value("functionResponse").toObject();
    const QJsonArray nested = functionResponse.value("parts").toArray();
    ASSERT_EQ(nested.size(), 1);

    const QJsonObject inlineData = nested[0].toObject().value("inlineData").toObject();
    EXPECT_EQ(inlineData.value("mimeType").toString(), "image/png");
    EXPECT_EQ(
        QByteArray::fromBase64(inlineData.value("data").toString().toUtf8()),
        QByteArray("PNGDATA"));
}

TEST(Conversation, ResponsesCarriesImageToolResultsAsInputImage)
{
    OpenAIResponsesClient client("https://x", "k", "gpt-5");
    const QJsonArray input
        = client.buildConversationPayload(imageToolResultConversation()).value("input").toArray();

    const QJsonObject output = input.last().toObject();
    ASSERT_EQ(output.value("type").toString(), "function_call_output");

    const QJsonArray blocks = output.value("output").toArray();
    ASSERT_EQ(blocks.size(), 2);
    EXPECT_EQ(blocks[0].toObject().value("type").toString(), "input_text");
    EXPECT_EQ(blocks[1].toObject().value("type").toString(), "input_image");
    EXPECT_TRUE(blocks[1]
                    .toObject()
                    .value("image_url")
                    .toString()
                    .startsWith("data:image/png;base64,"));
}

TEST(Conversation, ClaudeCarriesImageToolResultsAsSourceBlocks)
{
    ClaudeClient client("https://x", "k", "claude");
    const QJsonArray messages
        = client.buildConversationPayload(imageToolResultConversation()).value("messages").toArray();

    const QJsonArray blocks = messages.last().toObject().value("content").toArray();
    ASSERT_EQ(blocks.size(), 1);

    const QJsonArray inner = blocks[0].toObject().value("content").toArray();
    ASSERT_EQ(inner.size(), 2);
    EXPECT_EQ(inner[0].toObject().value("type").toString(), "text");
    EXPECT_EQ(inner[1].toObject().value("type").toString(), "image");
    EXPECT_EQ(
        inner[1].toObject().value("source").toObject().value("media_type").toString(),
        "image/png");
}
