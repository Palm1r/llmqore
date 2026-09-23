// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include "clients/google/GoogleMessage.hpp"

using namespace LLMQore;

namespace {

QJsonObject candidateChunk(const QJsonArray &parts, const QString &finishReason = {})
{
    QJsonObject candidate = {{"content", QJsonObject{{"role", "model"}, {"parts", parts}}}};
    if (!finishReason.isEmpty())
        candidate["finishReason"] = finishReason;
    return QJsonObject{{"candidates", QJsonArray{candidate}}};
}

QJsonObject textPart(const QString &text)
{
    return QJsonObject{{"text", text}};
}

QJsonObject thoughtPart(const QString &text)
{
    return QJsonObject{{"text", text}, {"thought", true}};
}

QJsonObject signaturePart(const QString &signature)
{
    return QJsonObject{{"thoughtSignature", signature}};
}

QJsonObject functionCallPart(const QString &name, const QJsonObject &args = {})
{
    QJsonObject call = {{"name", name}};
    if (!args.isEmpty())
        call["args"] = args;
    return QJsonObject{{"functionCall", call}};
}

QJsonObject finishChunk(const QString &reason)
{
    return candidateChunk(QJsonArray{}, reason);
}

} // namespace

TEST(GoogleMessage, InitialState)
{
    GoogleMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
    EXPECT_TRUE(msg.stopReason().isEmpty());
}

TEST(GoogleMessage, TextPartsAccumulateAndAreHandedBackAsChunks)
{
    GoogleMessage msg;
    const MessageEffects first = msg.applyEvent(candidateChunk({textPart("Hello ")}));
    const MessageEffects second = msg.applyEvent(candidateChunk({textPart("world")}));

    EXPECT_EQ(first.chunk, "Hello ");
    EXPECT_EQ(second.chunk, "world");
    EXPECT_TRUE(first.thinkingCompleted);

    ASSERT_EQ(msg.currentBlocks().size(), 1);
    auto *textBlock = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text, "Hello world");
}

TEST(GoogleMessage, TextAfterAThoughtStartsANewTextBlock)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({textPart("text1")}));
    msg.applyEvent(candidateChunk({thoughtPart("thinking...")}));
    msg.applyEvent(candidateChunk({textPart("text2")}));

    ASSERT_EQ(msg.currentBlocks().size(), 3);
    auto *text1 = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    auto *thinking = std::get_if<ThinkingContent>(&msg.currentBlocks()[1]);
    auto *text2 = std::get_if<TextContent>(&msg.currentBlocks()[2]);
    ASSERT_NE(text1, nullptr);
    ASSERT_NE(thinking, nullptr);
    ASSERT_NE(text2, nullptr);
    EXPECT_EQ(text1->text, "text1");
    EXPECT_EQ(text2->text, "text2");
}

TEST(GoogleMessage, ThoughtPartsAccumulateAndAreNotChunks)
{
    GoogleMessage msg;
    const MessageEffects effects = msg.applyEvent(candidateChunk({thoughtPart("Let me think")}));
    msg.applyEvent(candidateChunk({thoughtPart("... more")}));

    EXPECT_TRUE(effects.chunk.isEmpty());
    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].thinking, "Let me think... more");
}

TEST(GoogleMessage, ThoughtSignatureLandsOnTheExistingThinkingBlock)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({thoughtPart("thinking"), signaturePart("sig123")}));

    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].signature, "sig123");
}

TEST(GoogleMessage, ThoughtSignatureWithoutAThoughtCreatesAThinkingBlock)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({signaturePart("sig456")}));

    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].signature, "sig456");
}

TEST(GoogleMessage, FunctionCallBecomesAToolBlockWithAGeneratedId)
{
    GoogleMessage msg;
    const MessageEffects effects = msg.applyEvent(
        candidateChunk({functionCallPart("read_file", QJsonObject{{"path", "/tmp/test.txt"}})}));

    EXPECT_TRUE(effects.thinkingCompleted);
    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    const ToolUseContent tool = msg.currentToolUseContent()[0];
    EXPECT_EQ(tool.name, "read_file");
    EXPECT_EQ(tool.input["path"].toString(), "/tmp/test.txt");
    EXPECT_FALSE(tool.id.isEmpty());
}

TEST(GoogleMessage, FunctionCallWithoutArgsHasAnEmptyInput)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({functionCallPart("list_files")}));

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_TRUE(msg.currentToolUseContent()[0].input.isEmpty());
}

TEST(GoogleMessage, SeveralFunctionCallsInOneChunk)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk(
        {functionCallPart("read", QJsonObject{{"path", "a"}}),
         functionCallPart("write", QJsonObject{{"path", "b"}})}));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
    ASSERT_EQ(tools.size(), 2);
    EXPECT_EQ(tools[0].input["path"].toString(), "a");
    EXPECT_EQ(tools[1].input["path"].toString(), "b");
    EXPECT_NE(tools[0].id, tools[1].id);
}

TEST(GoogleMessage, FinishReason_STOP_NoTools)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({textPart("answer")}, "STOP"));
    EXPECT_EQ(msg.state(), MessageState::Complete);
    EXPECT_EQ(msg.stopReason(), "STOP");
}

TEST(GoogleMessage, FinishReason_STOP_WithTools)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({functionCallPart("tool")}, "STOP"));
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(GoogleMessage, FinishReason_MAX_TOKENS_WithTools)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({functionCallPart("tool")}, "MAX_TOKENS"));
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(GoogleMessage, FinishReason_MAX_TOKENS_NoTools)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({textPart("truncated")}, "MAX_TOKENS"));
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(GoogleMessage, FinishReason_OtherReason)
{
    GoogleMessage msg;
    msg.applyEvent(finishChunk("UNKNOWN_REASON"));
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(GoogleMessage, ErrorFinishReasonsAreHandedBackAsErrors)
{
    const QStringList errorReasons
        = {"SAFETY", "RECITATION", "MALFORMED_FUNCTION_CALL", "PROHIBITED_CONTENT", "SPII", "OTHER"};

    for (const QString &reason : errorReasons) {
        GoogleMessage msg;
        const MessageEffects effects = msg.applyEvent(finishChunk(reason));
        ASSERT_TRUE(effects.error.has_value()) << reason.toStdString();
        EXPECT_FALSE(effects.error->value("message").toString().isEmpty())
            << "Empty error message for: " << reason.toStdString();
    }
}

TEST(GoogleMessage, StopIsNotAnError)
{
    GoogleMessage msg;
    const MessageEffects effects = msg.applyEvent(finishChunk("STOP"));
    EXPECT_FALSE(effects.error.has_value());
}

TEST(GoogleMessage, TextBeforeABlockingFinishIsStillHandedBack)
{
    GoogleMessage msg;
    const MessageEffects effects = msg.applyEvent(candidateChunk({textPart("par")}, "SAFETY"));

    EXPECT_EQ(effects.chunk, "par");
    ASSERT_TRUE(effects.error.has_value());
    EXPECT_EQ(effects.error->value("message").toString(), "Response blocked by safety filters");
}

TEST(GoogleMessage, AnErrorObjectInTheStreamIsHandedBackAsAnError)
{
    GoogleMessage msg;
    const MessageEffects effects = msg.applyEvent(QJsonObject{
        {"error", QJsonObject{{"code", 500}, {"message", "boom"}, {"status", "INTERNAL"}}}});

    ASSERT_TRUE(effects.error.has_value());
    EXPECT_EQ(effects.error->value("message").toString(), "boom");
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(GoogleMessage, UsageMetadataIsHandedBack)
{
    GoogleMessage msg;
    QJsonObject chunk = candidateChunk({textPart("hi")});
    const MessageEffects without = msg.applyEvent(chunk);
    chunk["usageMetadata"] = QJsonObject{{"promptTokenCount", 3}, {"candidatesTokenCount", 5}};
    const MessageEffects with = msg.applyEvent(chunk);

    EXPECT_TRUE(without.usage.isEmpty());
    EXPECT_EQ(with.usage["usageMetadata"].toObject()["candidatesTokenCount"].toInt(), 5);
}

TEST(GoogleMessage, BufferedResponseTakesTheStreamPath)
{
    GoogleMessage msg;
    const MessageEffects effects = msg.applyResponse(candidateChunk(
        {thoughtPart("ponder"), textPart("answer"), functionCallPart("tool")}, "STOP"));

    EXPECT_EQ(effects.chunk, "answer");
    EXPECT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(GoogleMessage, ToProviderFormat_TextOnly)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({textPart("Hello")}));

    const QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "model");
    const QJsonArray parts = result["parts"].toArray();
    ASSERT_EQ(parts.size(), 1);
    EXPECT_EQ(parts[0].toObject()["text"].toString(), "Hello");
}

TEST(GoogleMessage, ToProviderFormat_FunctionCall)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({functionCallPart("read_file", QJsonObject{{"path", "/tmp"}})}));

    const QJsonArray parts = msg.toProviderFormat()["parts"].toArray();
    ASSERT_EQ(parts.size(), 1);
    EXPECT_TRUE(parts[0].toObject().contains("functionCall"));
    EXPECT_EQ(parts[0].toObject()["functionCall"].toObject()["name"].toString(), "read_file");
}

TEST(GoogleMessage, ToProviderFormat_ThinkingWithSignature)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({thoughtPart("hmm..."), signaturePart("sig-abc")}));

    const QJsonArray parts = msg.toProviderFormat()["parts"].toArray();
    ASSERT_EQ(parts.size(), 2);
    EXPECT_EQ(parts[0].toObject()["text"].toString(), "hmm...");
    EXPECT_TRUE(parts[0].toObject()["thought"].toBool());
    EXPECT_EQ(parts[1].toObject()["thoughtSignature"].toString(), "sig-abc");
}

TEST(GoogleMessage, ToProviderFormat_MixedContent)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({thoughtPart("thinking")}));
    msg.applyEvent(candidateChunk({textPart("answer")}));
    msg.applyEvent(candidateChunk({functionCallPart("tool")}));

    EXPECT_EQ(msg.toProviderFormat()["parts"].toArray().size(), 3);
}

TEST(GoogleMessage, ToProviderFormat_ThinkingSignature_OnFunctionCall)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk(
        {thoughtPart("let me think about this..."),
         signaturePart("sig-xyz-123"),
         functionCallPart("read_file", QJsonObject{{"path", "/tmp/test"}})}));

    const QJsonArray parts = msg.toProviderFormat()["parts"].toArray();
    ASSERT_EQ(parts.size(), 3);

    EXPECT_TRUE(parts[0].toObject()["thought"].toBool());
    EXPECT_EQ(parts[0].toObject()["text"].toString(), "let me think about this...");
    EXPECT_EQ(parts[1].toObject()["thoughtSignature"].toString(), "sig-xyz-123");

    const QJsonObject functionCallPart = parts[2].toObject();
    EXPECT_TRUE(functionCallPart.contains("functionCall"));
    EXPECT_TRUE(functionCallPart.contains("thoughtSignature"))
        << "functionCall part missing thoughtSignature";
    EXPECT_EQ(functionCallPart["thoughtSignature"].toString(), "sig-xyz-123");
    EXPECT_EQ(functionCallPart["functionCall"].toObject()["name"].toString(), "read_file");
}

TEST(GoogleMessage, ToProviderFormat_ThinkingSignature_StandaloneOnFunctionCall)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk(
        {signaturePart("sig-standalone"), functionCallPart("echo", QJsonObject{{"msg", "hi"}})}));

    const QJsonArray parts = msg.toProviderFormat()["parts"].toArray();

    bool foundFunctionCallWithSig = false;
    for (const QJsonValue &p : parts) {
        const QJsonObject partObj = p.toObject();
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
    msg.applyEvent(candidateChunk({thoughtPart("planning..."), signaturePart("sig-multi")}));
    msg.applyEvent(candidateChunk(
        {functionCallPart("read", QJsonObject{{"path", "a"}}),
         functionCallPart("write", QJsonObject{{"path", "b"}})}));

    const QJsonArray parts = msg.toProviderFormat()["parts"].toArray();

    int functionCallsWithSig = 0;
    for (const QJsonValue &p : parts) {
        const QJsonObject partObj = p.toObject();
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
    msg.applyEvent(candidateChunk({functionCallPart("read"), functionCallPart("write")}));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
    ASSERT_EQ(tools.size(), 2);

    QHash<QString, ToolResult> results;
    results[tools[0].id] = ToolResult::text("file content");
    results[tools[1].id] = ToolResult::text("write ok");

    const QJsonArray parts = msg.createToolResultParts(results);
    ASSERT_EQ(parts.size(), 2);

    for (const QJsonValue &val : parts) {
        const QJsonObject obj = val.toObject();
        EXPECT_TRUE(obj.contains("functionResponse"));
        const QJsonObject funcResp = obj["functionResponse"].toObject();
        EXPECT_TRUE(funcResp.contains("name"));
        EXPECT_TRUE(funcResp.contains("response"));
        EXPECT_TRUE(funcResp["response"].toObject().contains("result"));
    }
}

TEST(GoogleMessage, CreateToolResultParts_ImageBecomesNestedInlineDataPart)
{
    GoogleMessage msg;
    msg.applyEvent(candidateChunk({functionCallPart("get_sample_image")}));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
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
    msg.applyEvent(candidateChunk({functionCallPart("read"), functionCallPart("get_sample_image")}));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
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
    msg.applyEvent(candidateChunk({functionCallPart("read")}));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
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
    msg.applyEvent(candidateChunk({functionCallPart("record")}));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();

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
    msg.applyEvent(candidateChunk({textPart("old"), functionCallPart("tool")}, "STOP"));

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
