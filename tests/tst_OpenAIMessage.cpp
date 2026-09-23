// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include "clients/openai/OpenAIMessage.hpp"

using namespace LLMQore;

namespace {

QJsonObject chunk(const QJsonObject &delta, const QString &finishReason = {})
{
    QJsonObject choice = {{"index", 0}, {"delta", delta}};
    if (!finishReason.isEmpty())
        choice["finish_reason"] = finishReason;
    return QJsonObject{{"choices", QJsonArray{choice}}};
}

QJsonObject textChunk(const QString &text)
{
    return chunk(QJsonObject{{"content", text}});
}

QJsonObject toolCallStart(int index, const QString &id, const QString &name)
{
    return chunk(QJsonObject{
        {"tool_calls",
         QJsonArray{QJsonObject{
             {"index", index},
             {"id", id},
             {"type", "function"},
             {"function", QJsonObject{{"name", name}}}}}}});
}

QJsonObject toolCallArguments(int index, const QString &fragment)
{
    return chunk(QJsonObject{
        {"tool_calls",
         QJsonArray{
             QJsonObject{{"index", index}, {"function", QJsonObject{{"arguments", fragment}}}}}}});
}

QJsonObject finish(const QString &reason)
{
    return chunk(QJsonObject{}, reason);
}

QJsonObject bufferedChoice(const QJsonObject &message, const QString &finishReason)
{
    return QJsonObject{
        {"choices",
         QJsonArray{QJsonObject{
             {"index", 0}, {"message", message}, {"finish_reason", finishReason}}}}};
}

QJsonObject bufferedToolCall(const QJsonValue &id, const QString &name, const QString &arguments)
{
    QJsonObject call = {
        {"type", "function"},
        {"function", QJsonObject{{"name", name}, {"arguments", arguments}}}};
    if (!id.isUndefined())
        call["id"] = id;
    return call;
}

} // namespace

TEST(OpenAIMessage, InitialState)
{
    OpenAIMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
}

TEST(OpenAIMessage, TextDeltasAccumulateAndAreHandedBackAsChunks)
{
    OpenAIMessage msg;
    const MessageEffects first = msg.applyEvent(textChunk("Hello "));
    const MessageEffects second = msg.applyEvent(textChunk("world"));

    EXPECT_EQ(first.chunk, "Hello ");
    EXPECT_EQ(second.chunk, "world");
    EXPECT_TRUE(first.thinkingCompleted) << "text closes whatever reasoning came before it";

    ASSERT_EQ(msg.currentBlocks().size(), 1);
    auto *textBlock = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text, "Hello world");
}

TEST(OpenAIMessage, ReasoningDeltasOpenAThinkingBlockAndAreNotChunks)
{
    OpenAIMessage msg;
    const MessageEffects effects = msg.applyEvent(chunk(QJsonObject{{"reasoning_content", "ponder"}}));

    EXPECT_TRUE(effects.chunk.isEmpty());
    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent().first().thinking, "ponder");
}

TEST(OpenAIMessage, ToolCallStartOpensAToolBlock)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallStart(0, "call_123", "read_file"));

    EXPECT_EQ(msg.currentBlocks().size(), 1);
    ASSERT_EQ(msg.currentToolUseContent().size(), 1);

    const ToolUseContent toolBlock = msg.currentToolUseContent()[0];
    EXPECT_EQ(toolBlock.id, "call_123");
    EXPECT_EQ(toolBlock.name, "read_file");
}

TEST(OpenAIMessage, StreamedArgumentsAreParsedWhenTheTurnFinishes)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallStart(0, "call_1", "write_file"));
    msg.applyEvent(toolCallArguments(0, R"({"path":)"));
    msg.applyEvent(toolCallArguments(0, R"("/tmp/f"})"));
    msg.applyEvent(finish("tool_calls"));

    EXPECT_EQ(msg.currentToolUseContent()[0].input["path"].toString(), "/tmp/f");
}

TEST(OpenAIMessage, ArgumentsForAnUnknownIndexAreIgnored)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallArguments(99, R"({"key":"value"})"));
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(OpenAIMessage, FinishWithNoOpenCallsCreatesNothing)
{
    OpenAIMessage msg;
    msg.applyEvent(finish("stop"));
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(OpenAIMessage, ACallWithoutArgumentsKeepsAnEmptyInput)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallStart(0, "call_1", "no_args_tool"));
    msg.applyEvent(finish("tool_calls"));

    EXPECT_TRUE(msg.currentToolUseContent()[0].input.isEmpty());
}

TEST(OpenAIMessage, InvalidArgumentJsonLeavesAnEmptyInput)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallStart(0, "call_1", "tool"));
    msg.applyEvent(toolCallArguments(0, "not valid json{{{"));
    msg.applyEvent(finish("tool_calls"));

    EXPECT_TRUE(msg.currentToolUseContent()[0].input.isEmpty());
}

TEST(OpenAIMessage, EveryOpenCallIsCompletedWhenTheTurnFinishes)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallStart(0, "call_1", "tool_a"));
    msg.applyEvent(toolCallArguments(0, R"({"a": 1})"));
    msg.applyEvent(toolCallStart(1, "call_2", "tool_b"));
    msg.applyEvent(toolCallArguments(1, R"({"b": 2})"));

    msg.applyEvent(finish("tool_calls"));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
    ASSERT_EQ(tools.size(), 2);
    EXPECT_EQ(tools[0].id, "call_1");
    EXPECT_EQ(tools[0].input["a"].toInt(), 1);
    EXPECT_EQ(tools[1].id, "call_2");
    EXPECT_EQ(tools[1].input["b"].toInt(), 2);
}

TEST(OpenAIMessage, FinishReasonStopIsFinalAndReadiesTools)
{
    OpenAIMessage msg;
    msg.applyEvent(textChunk("Hello"));
    const MessageEffects effects = msg.applyEvent(finish("stop"));

    EXPECT_EQ(msg.state(), MessageState::Final);
    EXPECT_EQ(msg.stopReason(), "stop");
    EXPECT_TRUE(effects.toolsReady);
    EXPECT_TRUE(effects.thinkingCompleted);
}

TEST(OpenAIMessage, FinishReasonToolCallsRequiresToolExecution)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallStart(0, "call_1", "tool"));
    msg.applyEvent(finish("tool_calls"));
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OpenAIMessage, FinishReasonToolCallsWithoutToolBlocksIsComplete)
{
    OpenAIMessage msg;
    msg.applyEvent(textChunk("text only"));
    msg.applyEvent(finish("tool_calls"));
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(OpenAIMessage, OtherFinishReasonsAreComplete)
{
    OpenAIMessage msg;
    msg.applyEvent(finish("length"));
    EXPECT_EQ(msg.state(), MessageState::Complete);
    EXPECT_EQ(msg.stopReason(), "length");
}

TEST(OpenAIMessage, OnlyChunksWithUsageHandItBack)
{
    OpenAIMessage msg;
    const MessageEffects text = msg.applyEvent(textChunk("hi"));
    const MessageEffects usage = msg.applyEvent(QJsonObject{
        {"choices", QJsonArray{}},
        {"usage", QJsonObject{{"prompt_tokens", 3}, {"completion_tokens", 5}}}});

    EXPECT_TRUE(text.usage.isEmpty());
    EXPECT_EQ(usage.usage["usage"].toObject()["completion_tokens"].toInt(), 5);
    EXPECT_EQ(msg.currentBlocks().size(), 1) << "a usage-only chunk adds no blocks";
}

TEST(OpenAIMessage, AnErrorChunkIsHandedBackAsAnError)
{
    OpenAIMessage msg;
    const MessageEffects object = msg.applyEvent(
        QJsonObject{{"error", QJsonObject{{"message", "boom"}, {"code", 502}}}});
    const MessageEffects text = msg.applyEvent(QJsonObject{{"error", "plain"}});

    ASSERT_TRUE(object.error.has_value());
    EXPECT_EQ(object.error->value("message").toString(), "boom");
    ASSERT_TRUE(text.error.has_value());
    EXPECT_EQ(text.error->value("message").toString(), "plain");
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(OpenAIMessage, BufferedResponseReplaysTheChoiceAsAStreamChunk)
{
    OpenAIMessage msg;
    QJsonObject response = bufferedChoice(QJsonObject{{"content", "hello"}}, "stop");
    response["usage"] = QJsonObject{{"prompt_tokens", 1}, {"completion_tokens", 2}};

    const MessageEffects effects = msg.applyResponse(response);

    EXPECT_EQ(effects.chunk, "hello");
    EXPECT_TRUE(effects.toolsReady);
    EXPECT_FALSE(effects.usage.isEmpty());
    EXPECT_EQ(msg.state(), MessageState::Final);
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
}

TEST(OpenAIMessage, BufferedToolCallsAreKeyedByPositionAndAlwaysHaveAnId)
{
    OpenAIMessage msg;
    QJsonObject first = bufferedToolCall("call_a", "read", R"({"path":"a"})");
    first["index"] = 0;
    QJsonObject second = bufferedToolCall("call_b", "read", R"({"path":"b"})");
    second["index"] = 0;
    const QJsonObject third = bufferedToolCall(QJsonValue::Undefined, "write", R"({"path":"c"})");

    msg.applyResponse(bufferedChoice(
        QJsonObject{{"content", QJsonValue()}, {"tool_calls", QJsonArray{first, second, third}}},
        "tool_calls"));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
    ASSERT_EQ(tools.size(), 3) << "a repeated server index must not merge two calls";
    EXPECT_EQ(tools[0].id, "call_a");
    EXPECT_EQ(tools[0].input["path"].toString(), "a");
    EXPECT_EQ(tools[1].id, "call_b");
    EXPECT_EQ(tools[1].input["path"].toString(), "b");
    EXPECT_FALSE(tools[2].id.isEmpty()) << "a call without an id still needs one to be answered";
    EXPECT_EQ(tools[2].input["path"].toString(), "c");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OpenAIMessage, BufferedResponseWithoutChoicesIsAnError)
{
    OpenAIMessage msg;
    const MessageEffects effects = msg.applyResponse(QJsonObject{{"choices", QJsonArray{}}});

    ASSERT_TRUE(effects.error.has_value());
    EXPECT_EQ(effects.error->value("message").toString(), "Empty choices in buffered response");
}

TEST(OpenAIMessage, ToProviderFormat_TextOnly)
{
    OpenAIMessage msg;
    msg.applyEvent(textChunk("Hello"));

    const QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    EXPECT_EQ(result["content"].toString(), "Hello");
    EXPECT_FALSE(result.contains("tool_calls"));
}

TEST(OpenAIMessage, ToProviderFormat_NullContentWhenEmpty)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallStart(0, "call_1", "tool"));
    msg.applyEvent(toolCallArguments(0, R"({})"));
    msg.applyEvent(finish("tool_calls"));

    const QJsonObject result = msg.toProviderFormat();
    EXPECT_TRUE(result["content"].isNull());
    EXPECT_TRUE(result.contains("tool_calls"));
}

TEST(OpenAIMessage, ToProviderFormat_WithToolCalls)
{
    OpenAIMessage msg;
    msg.applyEvent(textChunk("I'll call a tool"));
    msg.applyEvent(toolCallStart(0, "call_1", "read_file"));
    msg.applyEvent(toolCallArguments(0, R"({"path": "/tmp"})"));
    msg.applyEvent(finish("tool_calls"));

    const QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    EXPECT_EQ(result["content"].toString(), "I'll call a tool");
    EXPECT_EQ(result["tool_calls"].toArray().size(), 1);
}

TEST(OpenAIMessage, CreateToolResultMessages)
{
    OpenAIMessage msg;
    msg.applyEvent(toolCallStart(0, "call_1", "read"));
    msg.applyEvent(toolCallStart(1, "call_2", "write"));

    QHash<QString, ToolResult> results;
    results["call_1"] = ToolResult::text("file content");
    results["call_2"] = ToolResult::text("write ok");

    const QJsonArray toolResults = msg.createToolResultMessages(results);
    ASSERT_EQ(toolResults.size(), 2);

    EXPECT_EQ(toolResults[0].toObject()["role"].toString(), "tool");
    EXPECT_EQ(toolResults[0].toObject()["tool_call_id"].toString(), "call_1");
    EXPECT_EQ(toolResults[0].toObject()["content"].toString(), "file content");
    EXPECT_EQ(toolResults[1].toObject()["tool_call_id"].toString(), "call_2");
    EXPECT_EQ(toolResults[1].toObject()["content"].toString(), "write ok");
}

TEST(OpenAIMessage, StartNewContinuationClearsTheTurn)
{
    OpenAIMessage msg;
    msg.applyEvent(textChunk("old text"));
    msg.applyEvent(toolCallStart(0, "call_1", "tool"));
    msg.applyEvent(finish("stop"));
    ASSERT_EQ(msg.state(), MessageState::Final);

    msg.startNewContinuation();

    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.stopReason().isEmpty());
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
}

TEST(OpenAIMessage, TextAndToolCallsMixed)
{
    OpenAIMessage msg;
    msg.applyEvent(textChunk("Thinking..."));
    msg.applyEvent(toolCallStart(0, "call_1", "search"));
    msg.applyEvent(toolCallArguments(0, R"({"q": "test"})"));
    msg.applyEvent(finish("tool_calls"));

    EXPECT_EQ(msg.currentBlocks().size(), 2);
    EXPECT_EQ(msg.currentToolUseContent().size(), 1);

    auto *textBlock = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text, "Thinking...");
}

TEST(OpenAIMessage, ImageContent_Base64_Accessors)
{
    ImageContent ic = ImageContent::fromBase64("YmFzZTY0ZGF0YQ==", "image/png");
    EXPECT_EQ(ic.bytes(), QByteArray("base64data"));
    EXPECT_EQ(ic.mimeType, "image/png");
    EXPECT_FALSE(ic.isUrl());
}

TEST(OpenAIMessage, ImageContent_Url_Accessors)
{
    ImageContent ic = ImageContent::fromUrl(QUrl("https://example.com/img.jpg"));
    EXPECT_EQ(ic.url().toString(), "https://example.com/img.jpg");
    EXPECT_TRUE(ic.isUrl());
}
