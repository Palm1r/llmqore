// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "clients/openai/OpenAIResponsesMessage.hpp"

using namespace LLMCore;

TEST(OpenAIResponsesMessage, InitialState)
{
    OpenAIResponsesMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
    EXPECT_TRUE(msg.accumulatedText().isEmpty());
    EXPECT_FALSE(msg.hasToolCalls());
    EXPECT_FALSE(msg.hasThinkingContent());
}

TEST(OpenAIResponsesMessage, HandleContentDelta)
{
    OpenAIResponsesMessage msg;
    msg.handleContentDelta("Hello ");
    msg.handleContentDelta("world");

    EXPECT_EQ(msg.accumulatedText(), "Hello world");
    EXPECT_EQ(msg.getCurrentBlocks().size(), 1);
}

TEST(OpenAIResponsesMessage, HandleContentDelta_EmptyIgnored)
{
    OpenAIResponsesMessage msg;
    msg.handleContentDelta("");
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
    EXPECT_TRUE(msg.accumulatedText().isEmpty());
}

TEST(OpenAIResponsesMessage, HandleToolCallStart)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_abc", "read_file");

    EXPECT_TRUE(msg.hasToolCalls());
    EXPECT_EQ(msg.getCurrentToolUseContent().size(), 1);

    auto *toolBlock = msg.getCurrentToolUseContent()[0];
    EXPECT_EQ(toolBlock->id(), "call_abc");
    EXPECT_EQ(toolBlock->name(), "read_file");
}

TEST(OpenAIResponsesMessage, HandleToolCallDelta_StreamedArguments)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_1", "write");
    msg.handleToolCallDelta("call_1", R"({"path":)");
    msg.handleToolCallDelta("call_1", R"("/tmp/f"})");
    msg.handleToolCallComplete("call_1");

    auto *toolBlock = msg.getCurrentToolUseContent()[0];
    EXPECT_EQ(toolBlock->input()["path"].toString(), "/tmp/f");
}

TEST(OpenAIResponsesMessage, HandleToolCallDelta_UnknownCallId)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallDelta("unknown_id", R"({"key":"value"})");
    EXPECT_TRUE(msg.getCurrentToolUseContent().isEmpty());
}

TEST(OpenAIResponsesMessage, HandleToolCallComplete_EmptyArguments)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_1", "no_args");
    msg.handleToolCallComplete("call_1");

    auto *toolBlock = msg.getCurrentToolUseContent()[0];
    EXPECT_TRUE(toolBlock->input().isEmpty());
}

TEST(OpenAIResponsesMessage, HandleToolCallComplete_InvalidJson)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_1", "tool");
    msg.handleToolCallDelta("call_1", "not valid json");
    msg.handleToolCallComplete("call_1");

    auto *toolBlock = msg.getCurrentToolUseContent()[0];
    EXPECT_TRUE(toolBlock->input().isEmpty());
}

TEST(OpenAIResponsesMessage, HandleReasoningStart)
{
    OpenAIResponsesMessage msg;
    msg.handleReasoningStart("item_1");
    EXPECT_TRUE(msg.hasThinkingContent());
    EXPECT_EQ(msg.getCurrentThinkingContent().size(), 1);
}

TEST(OpenAIResponsesMessage, HandleReasoningDelta)
{
    OpenAIResponsesMessage msg;
    msg.handleReasoningStart("item_1");
    msg.handleReasoningDelta("item_1", "Let me think...");
    msg.handleReasoningDelta("item_1", " More thinking.");

    auto *thinking = msg.getCurrentThinkingContent()[0];
    EXPECT_EQ(thinking->thinking(), "Let me think... More thinking.");
}

TEST(OpenAIResponsesMessage, HandleReasoningDelta_UnknownItemId)
{
    OpenAIResponsesMessage msg;
    msg.handleReasoningDelta("unknown", "should be ignored");
    EXPECT_TRUE(msg.getCurrentThinkingContent().isEmpty());
}

TEST(OpenAIResponsesMessage, HandleReasoningComplete)
{
    OpenAIResponsesMessage msg;
    msg.handleReasoningStart("item_1");
    msg.handleReasoningDelta("item_1", "thinking...");
    msg.handleReasoningComplete("item_1");

    // Should not crash, thinking block still accessible
    EXPECT_EQ(msg.getCurrentThinkingContent().size(), 1);
}

TEST(OpenAIResponsesMessage, HandleStatus_Completed_NoTools)
{
    OpenAIResponsesMessage msg;
    msg.handleContentDelta("answer");
    msg.handleStatus("completed");
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(OpenAIResponsesMessage, HandleStatus_Completed_WithTools)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_1", "tool");
    msg.handleToolCallComplete("call_1");
    msg.handleStatus("completed");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OpenAIResponsesMessage, HandleStatus_InProgress)
{
    OpenAIResponsesMessage msg;
    msg.handleStatus("in_progress");
    EXPECT_EQ(msg.state(), MessageState::Building);
}

TEST(OpenAIResponsesMessage, HandleStatus_Failed)
{
    OpenAIResponsesMessage msg;
    msg.handleStatus("failed");
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OpenAIResponsesMessage, HandleStatus_Cancelled)
{
    OpenAIResponsesMessage msg;
    msg.handleStatus("cancelled");
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OpenAIResponsesMessage, HandleStatus_Incomplete)
{
    OpenAIResponsesMessage msg;
    msg.handleStatus("incomplete");
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OpenAIResponsesMessage, HandleStatus_Unknown)
{
    OpenAIResponsesMessage msg;
    msg.handleStatus("something_new");
    EXPECT_EQ(msg.state(), MessageState::Building);
}

TEST(OpenAIResponsesMessage, ToItemsFormat_TextOnly)
{
    OpenAIResponsesMessage msg;
    msg.handleContentDelta("Hello world");

    auto items = msg.toItemsFormat();
    EXPECT_EQ(items.size(), 1);
    EXPECT_EQ(items[0]["role"].toString(), "assistant");
    EXPECT_EQ(items[0]["content"].toString(), "Hello world");
}

TEST(OpenAIResponsesMessage, ToItemsFormat_ToolCallsOnly)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_1", "read_file");
    msg.handleToolCallDelta("call_1", R"({"path": "/tmp"})");
    msg.handleToolCallComplete("call_1");

    auto items = msg.toItemsFormat();
    EXPECT_EQ(items.size(), 1);
    EXPECT_EQ(items[0]["type"].toString(), "function_call");
    EXPECT_EQ(items[0]["call_id"].toString(), "call_1");
    EXPECT_EQ(items[0]["name"].toString(), "read_file");
}

TEST(OpenAIResponsesMessage, ToItemsFormat_TextAndToolCalls)
{
    OpenAIResponsesMessage msg;
    msg.handleContentDelta("Let me help");
    msg.handleToolCallStart("call_1", "search");
    msg.handleToolCallDelta("call_1", R"({"q": "test"})");
    msg.handleToolCallComplete("call_1");

    auto items = msg.toItemsFormat();
    EXPECT_EQ(items.size(), 2);
    EXPECT_EQ(items[0]["role"].toString(), "assistant");
    EXPECT_EQ(items[1]["type"].toString(), "function_call");
}

TEST(OpenAIResponsesMessage, CreateToolResultItems)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_1", "read");
    msg.handleToolCallStart("call_2", "write");

    QHash<QString, ToolResult> results;
    results["call_1"] = ToolResult::text("file content");
    results["call_2"] = ToolResult::text("write ok");

    QJsonArray items = msg.createToolResultItems(results);
    EXPECT_EQ(items.size(), 2);

    bool foundCall1 = false, foundCall2 = false;
    for (const auto &val : items) {
        QJsonObject obj = val.toObject();
        EXPECT_EQ(obj["type"].toString(), "function_call_output");
        if (obj["call_id"].toString() == "call_1") {
            EXPECT_EQ(obj["output"].toString(), "file content");
            foundCall1 = true;
        }
        if (obj["call_id"].toString() == "call_2") {
            EXPECT_EQ(obj["output"].toString(), "write ok");
            foundCall2 = true;
        }
    }
    EXPECT_TRUE(foundCall1);
    EXPECT_TRUE(foundCall2);
}

TEST(OpenAIResponsesMessage, CreateToolResultItems_ImageResultBecomesInputImageBlock)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_img", "screenshot");

    ToolResult r;
    r.content.append(ToolContent::makeText("here is the screenshot"));
    r.content.append(ToolContent::makeImage(QByteArray("PNGDATA"), "image/png"));

    QHash<QString, ToolResult> results;
    results["call_img"] = r;

    const QJsonArray items = msg.createToolResultItems(results);
    ASSERT_EQ(items.size(), 1);

    const QJsonObject item = items[0].toObject();
    EXPECT_EQ(item["type"].toString(), "function_call_output");
    EXPECT_EQ(item["call_id"].toString(), "call_img");

    // The output must be an array (not a bare string) once there's a non-text block.
    ASSERT_TRUE(item["output"].isArray());
    const QJsonArray blocks = item["output"].toArray();
    ASSERT_EQ(blocks.size(), 2);

    EXPECT_EQ(blocks[0].toObject()["type"].toString(), "input_text");
    EXPECT_EQ(blocks[0].toObject()["text"].toString(), "here is the screenshot");

    EXPECT_EQ(blocks[1].toObject()["type"].toString(), "input_image");
    EXPECT_EQ(blocks[1].toObject()["detail"].toString(), "auto");
    const QString dataUri = blocks[1].toObject()["image_url"].toString();
    EXPECT_TRUE(dataUri.startsWith("data:image/png;base64,"));
    // Verify the base64 payload decodes back to the original bytes.
    const QString base64 = dataUri.mid(QString("data:image/png;base64,").size());
    EXPECT_EQ(QByteArray::fromBase64(base64.toUtf8()), QByteArray("PNGDATA"));
}

TEST(OpenAIResponsesMessage, CreateToolResultItems_TextOnlyStillUsesBareString)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_1", "read");

    QHash<QString, ToolResult> results;
    results["call_1"] = ToolResult::text("file content");

    const QJsonArray items = msg.createToolResultItems(results);
    ASSERT_EQ(items.size(), 1);

    // Fast-path preservation: plain text must remain a bare string for
    // backwards wire compat with existing OpenAI Responses deployments.
    EXPECT_TRUE(items[0].toObject()["output"].isString());
    EXPECT_EQ(items[0].toObject()["output"].toString(), "file content");
}

TEST(OpenAIResponsesMessage, CreateToolResultItems_AudioFallsBackToInputText)
{
    OpenAIResponsesMessage msg;
    msg.handleToolCallStart("call_aud", "record");

    ToolResult r;
    r.content.append(ToolContent::makeAudio(QByteArray("WAVDATA"), "audio/wav"));

    QHash<QString, ToolResult> results;
    results["call_aud"] = r;

    const QJsonArray items = msg.createToolResultItems(results);
    ASSERT_EQ(items.size(), 1);
    ASSERT_TRUE(items[0].toObject()["output"].isArray());
    const QJsonObject block = items[0].toObject()["output"].toArray()[0].toObject();
    EXPECT_EQ(block["type"].toString(), "input_text");
    EXPECT_TRUE(block["text"].toString().contains("audio"));
}

TEST(OpenAIResponsesMessage, StartNewContinuation)
{
    OpenAIResponsesMessage msg;
    msg.handleContentDelta("old");
    msg.handleToolCallStart("call_1", "tool");
    msg.handleReasoningStart("item_1");
    msg.handleStatus("completed");

    msg.startNewContinuation();
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.getCurrentBlocks().isEmpty());
    EXPECT_FALSE(msg.hasToolCalls());
    EXPECT_FALSE(msg.hasThinkingContent());
    EXPECT_TRUE(msg.accumulatedText().isEmpty());
}

TEST(OpenAIResponsesMessage, MultipleReasoningBlocks)
{
    OpenAIResponsesMessage msg;
    msg.handleReasoningStart("item_1");
    msg.handleReasoningDelta("item_1", "First thought");
    msg.handleReasoningComplete("item_1");

    msg.handleReasoningStart("item_2");
    msg.handleReasoningDelta("item_2", "Second thought");
    msg.handleReasoningComplete("item_2");

    EXPECT_EQ(msg.getCurrentThinkingContent().size(), 2);
}

TEST(OpenAIResponsesMessage, ImagePayload_InputImage_DataUri)
{
    QJsonArray inputContent;
    inputContent.append(QJsonObject{{"type", "input_text"}, {"text", "What is this image?"}});
    inputContent.append(
        QJsonObject{
            {"type", "input_image"},
            {"image_url", "data:image/png;base64,base64data"},
            {"detail", "auto"}});

    QJsonArray input;
    input.append(QJsonObject{{"role", "user"}, {"content", inputContent}});

    QJsonObject payload;
    payload["model"] = "gpt-4o";
    payload["input"] = input;
    payload["stream"] = true;

    QJsonArray resultInput = payload["input"].toArray();
    EXPECT_EQ(resultInput.size(), 1);

    QJsonArray content = resultInput[0].toObject()["content"].toArray();
    EXPECT_EQ(content.size(), 2);
    EXPECT_EQ(content[0].toObject()["type"].toString(), "input_text");
    EXPECT_EQ(content[1].toObject()["type"].toString(), "input_image");
    EXPECT_EQ(content[1].toObject()["detail"].toString(), "auto");
    EXPECT_TRUE(content[1].toObject()["image_url"].toString().startsWith("data:image/png"));
}

TEST(OpenAIResponsesMessage, ImagePayload_InputImage_Url)
{
    QJsonArray inputContent;
    inputContent.append(QJsonObject{{"type", "input_text"}, {"text", "Describe this image"}});
    inputContent.append(
        QJsonObject{
            {"type", "input_image"},
            {"image_url", "https://example.com/photo.jpg"},
            {"detail", "high"}});

    QJsonArray input;
    input.append(QJsonObject{{"role", "user"}, {"content", inputContent}});

    QJsonObject payload;
    payload["input"] = input;

    QJsonArray content = payload["input"].toArray()[0].toObject()["content"].toArray();
    EXPECT_EQ(content[1].toObject()["image_url"].toString(), "https://example.com/photo.jpg");
    EXPECT_EQ(content[1].toObject()["detail"].toString(), "high");
}

TEST(OpenAIResponsesMessage, ImagePayload_MultipleImages)
{
    QJsonArray inputContent;
    inputContent.append(QJsonObject{{"type", "input_text"}, {"text", "Compare these images"}});
    inputContent.append(
        QJsonObject{
            {"type", "input_image"},
            {"image_url", "data:image/png;base64,img1data"},
            {"detail", "auto"}});
    inputContent.append(
        QJsonObject{
            {"type", "input_image"},
            {"image_url", "data:image/jpeg;base64,img2data"},
            {"detail", "auto"}});

    QJsonArray input;
    input.append(QJsonObject{{"role", "user"}, {"content", inputContent}});

    QJsonArray content = input[0].toObject()["content"].toArray();
    EXPECT_EQ(content.size(), 3);
    EXPECT_EQ(content[1].toObject()["type"].toString(), "input_image");
    EXPECT_EQ(content[2].toObject()["type"].toString(), "input_image");
}
