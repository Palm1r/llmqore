// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include "clients/openai/OpenAIMessage.hpp"

using namespace LLMCore;

TEST(OpenAIMessage, InitialState)
{
    OpenAIMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
    EXPECT_TRUE(msg.getCurrentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.getCurrentThinkingContent().isEmpty());
}

TEST(OpenAIMessage, HandleContentDelta)
{
    OpenAIMessage msg;
    msg.handleContentDelta("Hello ");
    msg.handleContentDelta("world");

    EXPECT_EQ(msg.getCurrentBlocks().size(), 1);
    auto *textBlock = dynamic_cast<TextContent *>(msg.getCurrentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text(), "Hello world");
}

TEST(OpenAIMessage, HandleContentDelta_MultipleCallsAppend)
{
    OpenAIMessage msg;
    msg.handleContentDelta("a");
    msg.handleContentDelta("b");
    msg.handleContentDelta("c");

    auto *textBlock = dynamic_cast<TextContent *>(msg.getCurrentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text(), "abc");
}

TEST(OpenAIMessage, HandleToolCallStart)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_123", "read_file");

    EXPECT_EQ(msg.getCurrentBlocks().size(), 1);
    EXPECT_EQ(msg.getCurrentToolUseContent().size(), 1);

    auto *toolBlock = msg.getCurrentToolUseContent()[0];
    EXPECT_EQ(toolBlock->id(), "call_123");
    EXPECT_EQ(toolBlock->name(), "read_file");
}

TEST(OpenAIMessage, HandleToolCallDelta_StreamedArguments)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_1", "write_file");
    msg.handleToolCallDelta(0, R"({"path":)");
    msg.handleToolCallDelta(0, R"("/tmp/f"})");
    msg.handleToolCallComplete(0);

    auto *toolBlock = msg.getCurrentToolUseContent()[0];
    EXPECT_EQ(toolBlock->input()["path"].toString(), "/tmp/f");
}

TEST(OpenAIMessage, HandleToolCallDelta_UnknownIndex)
{
    OpenAIMessage msg;
    msg.handleToolCallDelta(99, R"({"key":"value"})");
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
}

TEST(OpenAIMessage, HandleToolCallComplete_UnknownIndex)
{
    OpenAIMessage msg;
    msg.handleToolCallComplete(42);
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
}

TEST(OpenAIMessage, HandleToolCallComplete_EmptyArguments)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_1", "no_args_tool");
    msg.handleToolCallComplete(0);

    auto *toolBlock = msg.getCurrentToolUseContent()[0];
    EXPECT_TRUE(toolBlock->input().isEmpty());
}

TEST(OpenAIMessage, HandleToolCallComplete_InvalidJson)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_1", "tool");
    msg.handleToolCallDelta(0, "not valid json{{{");
    msg.handleToolCallComplete(0);

    auto *toolBlock = msg.getCurrentToolUseContent()[0];
    EXPECT_TRUE(toolBlock->input().isEmpty());
}

TEST(OpenAIMessage, CompleteAllPendingToolCalls)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_1", "tool_a");
    msg.handleToolCallDelta(0, R"({"a": 1})");
    msg.handleToolCallStart(1, "call_2", "tool_b");
    msg.handleToolCallDelta(1, R"({"b": 2})");

    msg.completeAllPendingToolCalls();

    auto tools = msg.getCurrentToolUseContent();
    EXPECT_EQ(tools.size(), 2);

    bool foundA = false, foundB = false;
    for (auto *tool : tools) {
        if (tool->id() == "call_1") {
            EXPECT_EQ(tool->input()["a"].toInt(), 1);
            foundA = true;
        }
        if (tool->id() == "call_2") {
            EXPECT_EQ(tool->input()["b"].toInt(), 2);
            foundB = true;
        }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
}

TEST(OpenAIMessage, HandleFinishReason_Stop)
{
    OpenAIMessage msg;
    msg.handleContentDelta("Hello");
    msg.handleFinishReason("stop");
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OpenAIMessage, HandleFinishReason_ToolCalls)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_1", "tool");
    msg.handleFinishReason("tool_calls");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OpenAIMessage, HandleFinishReason_ToolCallsWithoutToolBlocks)
{
    OpenAIMessage msg;
    msg.handleContentDelta("text only");
    msg.handleFinishReason("tool_calls");
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(OpenAIMessage, HandleFinishReason_Other)
{
    OpenAIMessage msg;
    msg.handleFinishReason("length");
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(OpenAIMessage, ToProviderFormat_TextOnly)
{
    OpenAIMessage msg;
    msg.handleContentDelta("Hello");

    QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    EXPECT_EQ(result["content"].toString(), "Hello");
    EXPECT_FALSE(result.contains("tool_calls"));
}

TEST(OpenAIMessage, ToProviderFormat_NullContentWhenEmpty)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_1", "tool");
    msg.handleToolCallDelta(0, R"({})");
    msg.handleToolCallComplete(0);

    QJsonObject result = msg.toProviderFormat();
    EXPECT_TRUE(result["content"].isNull());
    EXPECT_TRUE(result.contains("tool_calls"));
}

TEST(OpenAIMessage, ToProviderFormat_WithToolCalls)
{
    OpenAIMessage msg;
    msg.handleContentDelta("I'll call a tool");
    msg.handleToolCallStart(0, "call_1", "read_file");
    msg.handleToolCallDelta(0, R"({"path": "/tmp"})");
    msg.handleToolCallComplete(0);

    QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    EXPECT_EQ(result["content"].toString(), "I'll call a tool");

    QJsonArray toolCalls = result["tool_calls"].toArray();
    EXPECT_EQ(toolCalls.size(), 1);
}

TEST(OpenAIMessage, CreateToolResultMessages)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_1", "read");
    msg.handleToolCallStart(1, "call_2", "write");

    QHash<QString, ToolResult> results;
    results["call_1"] = ToolResult::text("file content");
    results["call_2"] = ToolResult::text("write ok");

    QJsonArray toolResults = msg.createToolResultMessages(results);
    EXPECT_EQ(toolResults.size(), 2);

    bool foundCall1 = false, foundCall2 = false;
    for (const auto &val : toolResults) {
        QJsonObject obj = val.toObject();
        EXPECT_EQ(obj["role"].toString(), "tool");
        if (obj["tool_call_id"].toString() == "call_1") {
            EXPECT_EQ(obj["content"].toString(), "file content");
            foundCall1 = true;
        }
        if (obj["tool_call_id"].toString() == "call_2") {
            EXPECT_EQ(obj["content"].toString(), "write ok");
            foundCall2 = true;
        }
    }
    EXPECT_TRUE(foundCall1);
    EXPECT_TRUE(foundCall2);
}

TEST(OpenAIMessage, StartNewContinuation)
{
    OpenAIMessage msg;
    msg.handleContentDelta("old text");
    msg.handleToolCallStart(0, "call_1", "tool");
    msg.handleFinishReason("stop");
    EXPECT_EQ(msg.state(), MessageState::Final);

    msg.startNewContinuation();
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
    EXPECT_TRUE(msg.getCurrentToolUseContent().isEmpty());
}

TEST(OpenAIMessage, MultipleToolCalls)
{
    OpenAIMessage msg;
    msg.handleToolCallStart(0, "call_1", "tool_a");
    msg.handleToolCallStart(1, "call_2", "tool_b");
    msg.handleToolCallDelta(0, R"({"x": 1})");
    msg.handleToolCallDelta(1, R"({"y": 2})");
    msg.handleToolCallComplete(0);
    msg.handleToolCallComplete(1);

    auto tools = msg.getCurrentToolUseContent();
    EXPECT_EQ(tools.size(), 2);
}

TEST(OpenAIMessage, TextAndToolCallsMixed)
{
    OpenAIMessage msg;
    msg.handleContentDelta("Thinking...");
    msg.handleToolCallStart(0, "call_1", "search");
    msg.handleToolCallDelta(0, R"({"q": "test"})");
    msg.handleToolCallComplete(0);

    EXPECT_EQ(msg.getCurrentBlocks().size(), 2);
    EXPECT_EQ(msg.getCurrentToolUseContent().size(), 1);

    auto *textBlock = dynamic_cast<TextContent *>(msg.getCurrentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text(), "Thinking...");
}

TEST(OpenAIMessage, ImageContent_Base64_Accessors)
{
    ImageContent ic("base64data", "image/png", ImageContent::ImageSourceType::Base64);
    EXPECT_EQ(ic.data(), "base64data");
    EXPECT_EQ(ic.mediaType(), "image/png");
    EXPECT_EQ(ic.sourceType(), ImageContent::ImageSourceType::Base64);
}

TEST(OpenAIMessage, ImageContent_Url_Accessors)
{
    ImageContent ic("https://example.com/img.jpg", "", ImageContent::ImageSourceType::Url);
    EXPECT_EQ(ic.data(), "https://example.com/img.jpg");
    EXPECT_EQ(ic.sourceType(), ImageContent::ImageSourceType::Url);
}
