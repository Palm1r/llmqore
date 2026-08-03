// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include "clients/google/GoogleMessage.hpp"

using namespace LLMQore;

TEST(GoogleMessage, InitialState)
{
    GoogleMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
    EXPECT_TRUE(msg.stopReason().isEmpty());
}

TEST(GoogleMessage, HandleContentDelta)
{
    GoogleMessage msg;
    msg.handleContentDelta("Hello ");
    msg.handleContentDelta("world");

    EXPECT_EQ(msg.currentBlocks().size(), 1);
    auto *textBlock = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text, "Hello world");
}

TEST(GoogleMessage, HandleContentDelta_CreatesNewBlockAfterNonText)
{
    GoogleMessage msg;
    msg.handleContentDelta("text1");
    msg.handleThoughtDelta("thinking...");
    msg.handleContentDelta("text2");

    EXPECT_EQ(msg.currentBlocks().size(), 3);
    auto *text1 = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    auto thinking = std::get_if<ThinkingContent>(&msg.currentBlocks()[1]);
    auto *text2 = std::get_if<TextContent>(&msg.currentBlocks()[2]);
    ASSERT_NE(text1, nullptr);
    ASSERT_NE(thinking, nullptr);
    ASSERT_NE(text2, nullptr);
    EXPECT_EQ(text1->text, "text1");
    EXPECT_EQ(text2->text, "text2");
}

TEST(GoogleMessage, HandleThoughtDelta)
{
    GoogleMessage msg;
    msg.handleThoughtDelta("Let me think");
    msg.handleThoughtDelta("... more");

    EXPECT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].thinking, "Let me think... more");
}

TEST(GoogleMessage, HandleThoughtSignature_ExistingBlock)
{
    GoogleMessage msg;
    msg.handleThoughtDelta("thinking");
    msg.handleThoughtSignature("sig123");

    auto thinking = msg.currentThinkingContent()[0];
    EXPECT_EQ(thinking.signature, "sig123");
}

TEST(GoogleMessage, HandleThoughtSignature_NoExistingBlock)
{
    GoogleMessage msg;
    msg.handleThoughtSignature("sig456");

    EXPECT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].signature, "sig456");
}

TEST(GoogleMessage, HandleFunctionCall_Complete)
{
    GoogleMessage msg;
    msg.handleToolCallStart("read_file");
    msg.handleToolCallDelta(R"({"path": "/tmp/test.txt"})");
    msg.handleToolCallComplete();

    EXPECT_EQ(msg.currentToolUseContent().size(), 1);
    auto tool = msg.currentToolUseContent()[0];
    EXPECT_EQ(tool.name, "read_file");
    EXPECT_EQ(tool.input["path"].toString(), "/tmp/test.txt");
    EXPECT_FALSE(tool.id.isEmpty()); // UUID generated
}

TEST(GoogleMessage, HandleFunctionCall_StreamedArgs)
{
    GoogleMessage msg;
    msg.handleToolCallStart("write_file");
    msg.handleToolCallDelta(R"({"path":)");
    msg.handleToolCallDelta(R"( "/tmp/f"})");
    msg.handleToolCallComplete();

    auto tool = msg.currentToolUseContent()[0];
    EXPECT_EQ(tool.input["path"].toString(), "/tmp/f");
}

TEST(GoogleMessage, HandleFunctionCall_EmptyArgs)
{
    GoogleMessage msg;
    msg.handleToolCallStart("list_files");
    msg.handleToolCallComplete();

    auto tool = msg.currentToolUseContent()[0];
    EXPECT_TRUE(tool.input.isEmpty());
}

TEST(GoogleMessage, HandleFunctionCall_InvalidJson)
{
    GoogleMessage msg;
    msg.handleToolCallStart("tool");
    msg.handleToolCallDelta("not json{{{");
    msg.handleToolCallComplete();

    auto tool = msg.currentToolUseContent()[0];
    EXPECT_TRUE(tool.input.isEmpty());
}

TEST(GoogleMessage, HandleFunctionCallComplete_NoFunctionStarted)
{
    GoogleMessage msg;
    msg.handleToolCallComplete();
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
}

TEST(GoogleMessage, HandleFunctionCall_MultipleCalls)
{
    GoogleMessage msg;
    msg.handleToolCallStart("read");
    msg.handleToolCallDelta(R"({"path": "a"})");
    msg.handleToolCallComplete();

    msg.handleToolCallStart("write");
    msg.handleToolCallDelta(R"({"path": "b"})");
    msg.handleToolCallComplete();

    EXPECT_EQ(msg.currentToolUseContent().size(), 2);
}

TEST(GoogleMessage, HandleFinishReason_STOP_NoTools)
{
    GoogleMessage msg;
    msg.handleContentDelta("answer");
    msg.handleStopReason("STOP");
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(GoogleMessage, HandleFinishReason_STOP_WithTools)
{
    GoogleMessage msg;
    msg.handleToolCallStart("tool");
    msg.handleToolCallComplete();
    msg.handleStopReason("STOP");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(GoogleMessage, HandleFinishReason_MAX_TOKENS_WithTools)
{
    GoogleMessage msg;
    msg.handleToolCallStart("tool");
    msg.handleToolCallComplete();
    msg.handleStopReason("MAX_TOKENS");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(GoogleMessage, HandleFinishReason_MAX_TOKENS_NoTools)
{
    GoogleMessage msg;
    msg.handleContentDelta("truncated");
    msg.handleStopReason("MAX_TOKENS");
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(GoogleMessage, HandleFinishReason_OtherReason)
{
    GoogleMessage msg;
    msg.handleStopReason("UNKNOWN_REASON");
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(GoogleMessage, IsErrorFinishReason_Safety)
{
    GoogleMessage msg;
    msg.handleStopReason("SAFETY");
    EXPECT_TRUE(msg.isErrorFinishReason());
    EXPECT_FALSE(msg.getErrorMessage().isEmpty());
}

TEST(GoogleMessage, IsErrorFinishReason_Recitation)
{
    GoogleMessage msg;
    msg.handleStopReason("RECITATION");
    EXPECT_TRUE(msg.isErrorFinishReason());
}

TEST(GoogleMessage, IsErrorFinishReason_MalformedFunctionCall)
{
    GoogleMessage msg;
    msg.handleStopReason("MALFORMED_FUNCTION_CALL");
    EXPECT_TRUE(msg.isErrorFinishReason());
}

TEST(GoogleMessage, IsErrorFinishReason_ProhibitedContent)
{
    GoogleMessage msg;
    msg.handleStopReason("PROHIBITED_CONTENT");
    EXPECT_TRUE(msg.isErrorFinishReason());
}

TEST(GoogleMessage, IsErrorFinishReason_SPII)
{
    GoogleMessage msg;
    msg.handleStopReason("SPII");
    EXPECT_TRUE(msg.isErrorFinishReason());
}

TEST(GoogleMessage, IsErrorFinishReason_Other)
{
    GoogleMessage msg;
    msg.handleStopReason("OTHER");
    EXPECT_TRUE(msg.isErrorFinishReason());
}

TEST(GoogleMessage, IsNotErrorFinishReason_STOP)
{
    GoogleMessage msg;
    msg.handleStopReason("STOP");
    EXPECT_FALSE(msg.isErrorFinishReason());
}

TEST(GoogleMessage, GetErrorMessage_AllTypes)
{
    const QStringList errorReasons
        = {"SAFETY", "RECITATION", "MALFORMED_FUNCTION_CALL", "PROHIBITED_CONTENT", "SPII", "OTHER"};

    for (const auto &reason : errorReasons) {
        GoogleMessage msg;
        msg.handleStopReason(reason);
        EXPECT_FALSE(msg.getErrorMessage().isEmpty())
            << "Empty error message for: " << reason.toStdString();
    }
}

TEST(GoogleMessage, GetErrorMessage_NoError)
{
    GoogleMessage msg;
    msg.handleStopReason("STOP");
    EXPECT_TRUE(msg.getErrorMessage().isEmpty());
}

TEST(GoogleMessage, ToProviderFormat_TextOnly)
{
    GoogleMessage msg;
    msg.handleContentDelta("Hello");

    QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "model");
    QJsonArray parts = result["parts"].toArray();
    EXPECT_EQ(parts.size(), 1);
    EXPECT_EQ(parts[0].toObject()["text"].toString(), "Hello");
}

TEST(GoogleMessage, ToProviderFormat_FunctionCall)
{
    GoogleMessage msg;
    msg.handleToolCallStart("read_file");
    msg.handleToolCallDelta(R"({"path": "/tmp"})");
    msg.handleToolCallComplete();

    QJsonObject result = msg.toProviderFormat();
    QJsonArray parts = result["parts"].toArray();
    EXPECT_EQ(parts.size(), 1);
    EXPECT_TRUE(parts[0].toObject().contains("functionCall"));
    EXPECT_EQ(parts[0].toObject()["functionCall"].toObject()["name"].toString(), "read_file");
}

TEST(GoogleMessage, ToProviderFormat_ThinkingWithSignature)
{
    GoogleMessage msg;
    msg.handleThoughtDelta("hmm...");
    msg.handleThoughtSignature("sig-abc");

    QJsonObject result = msg.toProviderFormat();
    QJsonArray parts = result["parts"].toArray();
    EXPECT_EQ(parts.size(), 2); // thinking part + signature part

    EXPECT_EQ(parts[0].toObject()["text"].toString(), "hmm...");
    EXPECT_TRUE(parts[0].toObject()["thought"].toBool());
    EXPECT_EQ(parts[1].toObject()["thoughtSignature"].toString(), "sig-abc");
}

TEST(GoogleMessage, ToProviderFormat_MixedContent)
{
    GoogleMessage msg;
    msg.handleThoughtDelta("thinking");
    msg.handleContentDelta("answer");
    msg.handleToolCallStart("tool");
    msg.handleToolCallDelta(R"({})");
    msg.handleToolCallComplete();

    QJsonObject result = msg.toProviderFormat();
    QJsonArray parts = result["parts"].toArray();
    EXPECT_EQ(parts.size(), 3); // thinking + text + functionCall
}

TEST(GoogleMessage, ToProviderFormat_ThinkingSignature_OnFunctionCall)
{
    GoogleMessage msg;
    msg.handleThoughtDelta("let me think about this...");
    msg.handleThoughtSignature("sig-xyz-123");
    msg.handleToolCallStart("read_file");
    msg.handleToolCallDelta(R"({"path": "/tmp/test"})");
    msg.handleToolCallComplete();

    QJsonObject result = msg.toProviderFormat();
    QJsonArray parts = result["parts"].toArray();
    EXPECT_EQ(parts.size(), 3);

    // Thinking part
    EXPECT_TRUE(parts[0].toObject()["thought"].toBool());
    EXPECT_EQ(parts[0].toObject()["text"].toString(), "let me think about this...");

    // Signature part
    EXPECT_EQ(parts[1].toObject()["thoughtSignature"].toString(), "sig-xyz-123");

    QJsonObject functionCallPart = parts[2].toObject();
    EXPECT_TRUE(functionCallPart.contains("functionCall"));
    EXPECT_TRUE(functionCallPart.contains("thoughtSignature"))
        << "functionCall part missing thoughtSignature";
    EXPECT_EQ(functionCallPart["thoughtSignature"].toString(), "sig-xyz-123");
    EXPECT_EQ(functionCallPart["functionCall"].toObject()["name"].toString(), "read_file");
}

TEST(GoogleMessage, ToProviderFormat_ThinkingSignature_StandaloneOnFunctionCall)
{
    GoogleMessage msg;
    msg.handleThoughtSignature("sig-standalone");
    msg.handleToolCallStart("echo");
    msg.handleToolCallDelta(R"({"msg": "hi"})");
    msg.handleToolCallComplete();

    QJsonObject result = msg.toProviderFormat();
    QJsonArray parts = result["parts"].toArray();

    bool foundFunctionCallWithSig = false;
    for (const auto &p : parts) {
        QJsonObject partObj = p.toObject();
        if (partObj.contains("functionCall")) {
            EXPECT_TRUE(partObj.contains("thoughtSignature"))
                << "functionCall part missing thoughtSignature";
            EXPECT_EQ(partObj["thoughtSignature"].toString(), "sig-standalone");
            foundFunctionCallWithSig = true;
        }
    }
    EXPECT_TRUE(foundFunctionCallWithSig) << "No functionCall part found";
}

TEST(GoogleMessage, ToProviderFormat_MultipleFunctionCalls_ShareSignature)
{
    GoogleMessage msg;
    msg.handleThoughtDelta("planning...");
    msg.handleThoughtSignature("sig-multi");

    msg.handleToolCallStart("read");
    msg.handleToolCallDelta(R"({"path": "a"})");
    msg.handleToolCallComplete();

    msg.handleToolCallStart("write");
    msg.handleToolCallDelta(R"({"path": "b"})");
    msg.handleToolCallComplete();

    QJsonObject result = msg.toProviderFormat();
    QJsonArray parts = result["parts"].toArray();

    int functionCallsWithSig = 0;
    for (const auto &p : parts) {
        QJsonObject partObj = p.toObject();
        if (partObj.contains("functionCall") && partObj.contains("thoughtSignature")) {
            EXPECT_EQ(partObj["thoughtSignature"].toString(), "sig-multi");
            functionCallsWithSig++;
        }
    }
    EXPECT_EQ(functionCallsWithSig, 2) << "Both function calls should have thoughtSignature";
}

TEST(GoogleMessage, CreateToolResultParts)
{
    GoogleMessage msg;
    msg.handleToolCallStart("read");
    msg.handleToolCallComplete();
    msg.handleToolCallStart("write");
    msg.handleToolCallComplete();

    auto tools = msg.currentToolUseContent();

    QHash<QString, ToolResult> results;
    results[tools[0].id] = ToolResult::text("file content");
    results[tools[1].id] = ToolResult::text("write ok");

    QJsonArray parts = msg.createToolResultParts(results);
    EXPECT_EQ(parts.size(), 2);

    for (const auto &val : parts) {
        QJsonObject obj = val.toObject();
        EXPECT_TRUE(obj.contains("functionResponse"));
        auto funcResp = obj["functionResponse"].toObject();
        EXPECT_TRUE(funcResp.contains("name"));
        EXPECT_TRUE(funcResp.contains("response"));
        EXPECT_TRUE(funcResp["response"].toObject().contains("result"));
    }
}

TEST(GoogleMessage, CreateToolResultParts_ImageBecomesNestedInlineDataPart)
{
    GoogleMessage msg;
    msg.handleToolCallStart("get_sample_image");
    msg.handleToolCallComplete();

    auto tools = msg.currentToolUseContent();
    ASSERT_EQ(tools.size(), 1);

    ToolResult r;
    r.content.append(TextContent{"here is the screenshot"});
    r.content.append(ImageContent::fromBytes(QByteArray("PNGDATA"), "image/png"));

    QHash<QString, ToolResult> results;
    results[tools[0].id] = r;

    const QJsonArray parts = msg.createToolResultParts(results);
    ASSERT_EQ(parts.size(), 1);

    const QJsonObject funcResp = parts[0].toObject()["functionResponse"].toObject();
    EXPECT_EQ(funcResp["name"].toString(), "get_sample_image");

    EXPECT_EQ(
        funcResp["response"].toObject()["result"].toString(), "here is the screenshot");

    const QJsonArray nested = funcResp["parts"].toArray();
    ASSERT_EQ(nested.size(), 1);
    const QJsonObject inlineData = nested[0].toObject()["inlineData"].toObject();
    EXPECT_EQ(inlineData["mimeType"].toString(), "image/png");
    EXPECT_EQ(
        QByteArray::fromBase64(inlineData["data"].toString().toUtf8()), QByteArray("PNGDATA"));

    EXPECT_EQ(GoogleMessage::toolResultTurnRole(parts), "user");
}

TEST(GoogleMessage, CreateToolResultParts_OnePartPerFunctionCallWithMixedResults)
{
    GoogleMessage msg;
    msg.handleToolCallStart("read");
    msg.handleToolCallComplete();
    msg.handleToolCallStart("get_sample_image");
    msg.handleToolCallComplete();

    auto tools = msg.currentToolUseContent();
    ASSERT_EQ(tools.size(), 2);

    ToolResult withImage;
    withImage.content.append(TextContent{"here is the screenshot"});
    withImage.content.append(ImageContent::fromBytes(QByteArray("PNGDATA"), "image/png"));

    QHash<QString, ToolResult> results;
    results[tools[0].id] = ToolResult::text("file content");
    results[tools[1].id] = withImage;

    const QJsonArray parts = msg.createToolResultParts(results);
    ASSERT_EQ(parts.size(), 2);

    for (const QJsonValue &val : parts)
        EXPECT_TRUE(val.toObject().contains("functionResponse"));

    EXPECT_EQ(GoogleMessage::toolResultTurnRole(parts), "user");
}

TEST(GoogleMessage, CreateToolResultParts_TextOnlyKeepsFlatResponse)
{
    GoogleMessage msg;
    msg.handleToolCallStart("read");
    msg.handleToolCallComplete();

    auto tools = msg.currentToolUseContent();
    QHash<QString, ToolResult> results;
    results[tools[0].id] = ToolResult::text("plain text result");

    const QJsonArray parts = msg.createToolResultParts(results);
    ASSERT_EQ(parts.size(), 1);

    const QJsonObject funcResp = parts[0].toObject()["functionResponse"].toObject();
    EXPECT_EQ(funcResp["response"].toObject()["result"].toString(), "plain text result");
    EXPECT_FALSE(funcResp.contains("parts"));

    EXPECT_EQ(GoogleMessage::toolResultTurnRole(parts), "function");
}

TEST(GoogleMessage, CreateToolResultParts_AudioAlsoBecomesInlineData)
{
    GoogleMessage msg;
    msg.handleToolCallStart("record");
    msg.handleToolCallComplete();

    auto tools = msg.currentToolUseContent();

    ToolResult r;
    r.content.append(AudioContent{QByteArray("WAVDATA"), "audio/wav"});

    QHash<QString, ToolResult> results;
    results[tools[0].id] = r;

    const QJsonArray parts = msg.createToolResultParts(results);
    ASSERT_EQ(parts.size(), 1);

    const QJsonObject funcResp = parts[0].toObject()["functionResponse"].toObject();
    const QJsonArray nested = funcResp["parts"].toArray();
    ASSERT_EQ(nested.size(), 1);

    const QJsonObject inlineData = nested[0].toObject()["inlineData"].toObject();
    EXPECT_EQ(inlineData["mimeType"].toString(), "audio/wav");
    EXPECT_EQ(
        QByteArray::fromBase64(inlineData["data"].toString().toUtf8()), QByteArray("WAVDATA"));
}

TEST(GoogleMessage, StartNewContinuation)
{
    GoogleMessage msg;
    msg.handleContentDelta("old");
    msg.handleToolCallStart("tool");
    msg.handleToolCallComplete();
    msg.handleStopReason("STOP");

    msg.startNewContinuation();
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.stopReason().isEmpty());
}

TEST(GoogleMessage, ImagePayload_InlineData)
{
    QJsonObject inlineData;
    inlineData["mimeType"] = "image/png";
    inlineData["data"] = "base64encodeddata";

    QJsonObject imagePart;
    imagePart["inlineData"] = inlineData;

    QJsonObject textPart;
    textPart["text"] = "What is in this image?";

    QJsonArray parts;
    parts.append(textPart);
    parts.append(imagePart);

    QJsonObject userContent;
    userContent["role"] = "user";
    userContent["parts"] = parts;

    QJsonArray resultParts = userContent["parts"].toArray();
    EXPECT_EQ(resultParts.size(), 2);
    EXPECT_TRUE(resultParts[0].toObject().contains("text"));
    EXPECT_TRUE(resultParts[1].toObject().contains("inlineData"));
    EXPECT_EQ(resultParts[1].toObject()["inlineData"].toObject()["mimeType"].toString(), "image/png");
}

TEST(GoogleMessage, ImagePayload_FileData)
{
    QJsonObject fileData;
    fileData["mimeType"] = "image/jpeg";
    fileData["fileUri"] = "gs://bucket/image.jpg";

    QJsonObject imagePart;
    imagePart["fileData"] = fileData;

    QJsonArray parts;
    parts.append(QJsonObject{{"text", "Describe this"}});
    parts.append(imagePart);

    QJsonObject content;
    content["role"] = "user";
    content["parts"] = parts;

    EXPECT_EQ(content["parts"].toArray().size(), 2);
    EXPECT_EQ(
        content["parts"].toArray()[1].toObject()["fileData"].toObject()["mimeType"].toString(),
        "image/jpeg");
}
