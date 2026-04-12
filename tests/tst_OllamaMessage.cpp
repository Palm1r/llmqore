// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include "clients/ollama/OllamaMessage.hpp"

using namespace LLMCore;

TEST(OllamaMessage, InitialState)
{
    OllamaMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
    EXPECT_TRUE(msg.getCurrentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.getCurrentThinkingContent().isEmpty());
}

TEST(OllamaMessage, HandleContentDelta_PlainText)
{
    OllamaMessage msg;
    msg.handleContentDelta("Hello ");
    msg.handleContentDelta("world");

    EXPECT_EQ(msg.getCurrentBlocks().size(), 1);
    auto *textBlock = dynamic_cast<TextContent *>(msg.getCurrentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text(), "Hello world");
}

TEST(OllamaMessage, HandleContentDelta_JsonLikeBuffered)
{
    OllamaMessage msg;
    msg.handleContentDelta(R"({"name": "read")");

    bool hasTextBlock = false;
    for (auto *block : msg.getCurrentBlocks()) {
        if (dynamic_cast<TextContent *>(block))
            hasTextBlock = true;
    }
    EXPECT_FALSE(hasTextBlock);
}

TEST(OllamaMessage, HandleContentDelta_NonJsonFlushesAccumulated)
{
    OllamaMessage msg;
    msg.handleContentDelta("Hello");

    auto *textBlock = dynamic_cast<TextContent *>(msg.getCurrentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text(), "Hello");
}

TEST(OllamaMessage, HandleToolCall_Structured)
{
    OllamaMessage msg;
    QJsonObject toolCall{
        {"function",
         QJsonObject{{"name", "read_file"}, {"arguments", QJsonObject{{"path", "/tmp"}}}}}};

    msg.handleToolCall(toolCall);

    EXPECT_EQ(msg.getCurrentToolUseContent().size(), 1);
    auto *tool = msg.getCurrentToolUseContent()[0];
    EXPECT_EQ(tool->name(), "read_file");
    EXPECT_EQ(tool->input()["path"].toString(), "/tmp");
    EXPECT_TRUE(tool->id().startsWith("call_read_file_"));
}

TEST(OllamaMessage, HandleToolCall_MultipleStructured)
{
    OllamaMessage msg;

    QJsonObject toolCall1{
        {"function", QJsonObject{{"name", "tool_a"}, {"arguments", QJsonObject{{"x", 1}}}}}};
    QJsonObject toolCall2{
        {"function", QJsonObject{{"name", "tool_b"}, {"arguments", QJsonObject{{"y", 2}}}}}};

    msg.handleToolCall(toolCall1);
    msg.handleToolCall(toolCall2);

    EXPECT_EQ(msg.getCurrentToolUseContent().size(), 2);
}

TEST(OllamaMessage, HandleDone_ParsesToolCallFromContent)
{
    OllamaMessage msg;
    msg.handleContentDelta(R"({"name": "read_file", "arguments": {"path": "/tmp/test.txt"}})");
    msg.handleDone(true);

    EXPECT_EQ(msg.getCurrentToolUseContent().size(), 1);
    auto *tool = msg.getCurrentToolUseContent()[0];
    EXPECT_EQ(tool->name(), "read_file");
    EXPECT_EQ(tool->input()["path"].toString(), "/tmp/test.txt");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OllamaMessage, HandleDone_ParsesToolCallWithStringArguments)
{
    OllamaMessage msg;
    msg.handleContentDelta(R"({"name": "tool", "arguments": "{\"key\": \"value\"}"})");
    msg.handleDone(true);

    EXPECT_EQ(msg.getCurrentToolUseContent().size(), 1);
    auto *tool = msg.getCurrentToolUseContent()[0];
    EXPECT_EQ(tool->input()["key"].toString(), "value");
}

TEST(OllamaMessage, HandleDone_PlainTextFinal)
{
    OllamaMessage msg;
    msg.handleContentDelta("Just a normal answer");
    msg.handleDone(true);

    EXPECT_EQ(msg.state(), MessageState::Final);
    EXPECT_EQ(msg.getCurrentToolUseContent().size(), 0);

    auto *textBlock = dynamic_cast<TextContent *>(msg.getCurrentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text(), "Just a normal answer");
}

TEST(OllamaMessage, HandleDone_FalseDoesNothing)
{
    OllamaMessage msg;
    msg.handleContentDelta("partial");
    msg.handleDone(false);

    EXPECT_EQ(msg.state(), MessageState::Building);
}

TEST(OllamaMessage, HandleDone_InvalidToolCallJson)
{
    OllamaMessage msg;
    msg.handleContentDelta(R"({"name": "", "arguments": {}})");
    msg.handleDone(true);

    EXPECT_TRUE(msg.getCurrentToolUseContent().isEmpty());
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OllamaMessage, HandleDone_IncompleteToolCallJsonDiscarded)
{
    OllamaMessage msg;
    msg.handleContentDelta(R"({"name": "tool", "arguments": )");
    msg.handleDone(true);

    EXPECT_TRUE(msg.getCurrentToolUseContent().isEmpty());
}

TEST(OllamaMessage, HandleDone_JsonWithoutToolFields)
{
    OllamaMessage msg;
    msg.handleContentDelta(R"({"key": "value", "other": 123})");
    msg.handleDone(true);

    EXPECT_TRUE(msg.getCurrentToolUseContent().isEmpty());
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OllamaMessage, HandleThinkingDelta)
{
    OllamaMessage msg;
    msg.handleThinkingDelta("Step 1...");
    msg.handleThinkingDelta(" Step 2...");

    auto thinkingBlocks = msg.getCurrentThinkingContent();
    EXPECT_EQ(thinkingBlocks.size(), 1);
    EXPECT_EQ(thinkingBlocks[0]->thinking(), "Step 1... Step 2...");
}

TEST(OllamaMessage, HandleThinkingComplete_WithSignature)
{
    OllamaMessage msg;
    msg.handleThinkingDelta("thinking...");
    msg.handleThinkingComplete("sig-abc");

    auto *thinking = msg.getCurrentThinkingContent()[0];
    EXPECT_EQ(thinking->signature(), "sig-abc");
}

TEST(OllamaMessage, HandleThinkingComplete_NoThinkingBlock)
{
    OllamaMessage msg;
    msg.handleThinkingComplete("sig");
    EXPECT_TRUE(msg.getCurrentThinkingContent().isEmpty());
}

TEST(OllamaMessage, HandleThinkingDelta_ReusesExistingBlock)
{
    OllamaMessage msg;
    msg.handleThinkingDelta("first");
    msg.handleThinkingDelta(" second");

    EXPECT_EQ(msg.getCurrentThinkingContent().size(), 1);
    EXPECT_EQ(msg.getCurrentThinkingContent()[0]->thinking(), "first second");
}

TEST(OllamaMessage, ToProviderFormat_TextOnly)
{
    OllamaMessage msg;
    msg.handleContentDelta("Hello world");
    msg.handleDone(true);

    QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    EXPECT_EQ(result["content"].toString(), "Hello world");
    EXPECT_FALSE(result.contains("tool_calls"));
}

TEST(OllamaMessage, ToProviderFormat_WithToolCalls)
{
    OllamaMessage msg;
    QJsonObject toolCall{
        {"function", QJsonObject{{"name", "read"}, {"arguments", QJsonObject{{"p", "a"}}}}}};
    msg.handleToolCall(toolCall);

    QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    QJsonArray toolCalls = result["tool_calls"].toArray();
    EXPECT_EQ(toolCalls.size(), 1);
    EXPECT_EQ(toolCalls[0].toObject()["type"].toString(), "function");
}

TEST(OllamaMessage, ToProviderFormat_WithThinking)
{
    OllamaMessage msg;
    msg.handleThinkingDelta("hmm...");
    msg.handleContentDelta("answer");
    msg.handleDone(true);

    QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["thinking"].toString(), "hmm...");
    EXPECT_EQ(result["content"].toString(), "answer");
}

TEST(OllamaMessage, CreateToolResultMessages)
{
    OllamaMessage msg;
    QJsonObject tc1{{"function", QJsonObject{{"name", "read"}, {"arguments", QJsonObject{}}}}};
    QJsonObject tc2{{"function", QJsonObject{{"name", "write"}, {"arguments", QJsonObject{}}}}};
    msg.handleToolCall(tc1);
    msg.handleToolCall(tc2);

    auto tools = msg.getCurrentToolUseContent();
    QHash<QString, ToolResult> results;
    results[tools[0]->id()] = ToolResult::text("content1");
    results[tools[1]->id()] = ToolResult::text("content2");

    QJsonArray messages = msg.createToolResultMessages(results);
    EXPECT_EQ(messages.size(), 2);

    for (const auto &val : messages) {
        QJsonObject obj = val.toObject();
        EXPECT_EQ(obj["role"].toString(), "tool");
        EXPECT_FALSE(obj["content"].toString().isEmpty());
    }
}

TEST(OllamaMessage, StateTransition_DoneWithTools)
{
    OllamaMessage msg;
    QJsonObject toolCall{{"function", QJsonObject{{"name", "tool"}, {"arguments", QJsonObject{}}}}};
    msg.handleToolCall(toolCall);
    msg.handleDone(true);

    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OllamaMessage, StateTransition_DoneWithoutTools)
{
    OllamaMessage msg;
    msg.handleContentDelta("answer");
    msg.handleDone(true);

    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OllamaMessage, StartNewContinuation)
{
    OllamaMessage msg;
    msg.handleContentDelta("old");
    msg.handleThinkingDelta("thought");
    QJsonObject toolCall{{"function", QJsonObject{{"name", "tool"}, {"arguments", QJsonObject{}}}}};
    msg.handleToolCall(toolCall);
    msg.handleDone(true);

    msg.startNewContinuation();
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
    EXPECT_TRUE(msg.getCurrentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.getCurrentThinkingContent().isEmpty());
}

TEST(OllamaMessage, ImagePayload_Base64Images)
{
    QJsonArray images;
    images.append(QString("base64encodedpng"));
    images.append(QString("base64encodedjpeg"));

    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = "What is in these images?";
    userMessage["images"] = images;

    EXPECT_EQ(userMessage["images"].toArray().size(), 2);
    EXPECT_EQ(userMessage["images"].toArray()[0].toString(), "base64encodedpng");
}

TEST(OllamaMessage, ImagePayload_InChatRequest)
{
    QJsonArray images;
    images.append(QString("imgdata"));

    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = "Describe this image";
    msg["images"] = images;

    QJsonObject payload;
    payload["model"] = "llava";
    payload["stream"] = true;
    payload["messages"] = QJsonArray{msg};

    QJsonArray messages = payload["messages"].toArray();
    EXPECT_EQ(messages.size(), 1);
    QJsonObject firstMsg = messages[0].toObject();
    EXPECT_EQ(firstMsg["role"].toString(), "user");
    EXPECT_TRUE(firstMsg.contains("images"));
    EXPECT_EQ(firstMsg["images"].toArray().size(), 1);
}
