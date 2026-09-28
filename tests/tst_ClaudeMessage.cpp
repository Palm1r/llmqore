// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include "clients/claude/ClaudeMessage.hpp"

using namespace LLMQore;

namespace {

QJsonObject messageStart()
{
    return QJsonObject{
        {"type", "message_start"},
        {"message",
         QJsonObject{
             {"type", "message"},
             {"role", "assistant"},
             {"content", QJsonArray{}},
             {"usage", QJsonObject{{"input_tokens", 12}, {"output_tokens", 1}}}}}};
}

QJsonObject blockStart(int index, const QJsonObject &block)
{
    return QJsonObject{{"type", "content_block_start"}, {"index", index}, {"content_block", block}};
}

QJsonObject blockDelta(int index, const QJsonObject &delta)
{
    return QJsonObject{{"type", "content_block_delta"}, {"index", index}, {"delta", delta}};
}

QJsonObject blockStop(int index)
{
    return QJsonObject{{"type", "content_block_stop"}, {"index", index}};
}

QJsonObject messageDelta(const QString &stopReason)
{
    return QJsonObject{
        {"type", "message_delta"},
        {"delta", QJsonObject{{"stop_reason", stopReason}, {"stop_sequence", QJsonValue()}}},
        {"usage", QJsonObject{{"output_tokens", 42}}}};
}

QJsonObject textBlock(const QString &text = {})
{
    return QJsonObject{{"type", "text"}, {"text", text}};
}

QJsonObject toolUseBlock(const QString &id, const QString &name, const QJsonObject &input = {})
{
    return QJsonObject{{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", input}};
}

QJsonObject base64ImageBlock(const QString &data, const QString &mediaType)
{
    return QJsonObject{
        {"type", "image"},
        {"source", QJsonObject{{"type", "base64"}, {"data", data}, {"media_type", mediaType}}}};
}

void startText(ClaudeMessage &msg, int index, const QString &text)
{
    msg.applyEvent(blockStart(index, textBlock()));
    if (!text.isEmpty())
        msg.applyEvent(blockDelta(index, QJsonObject{{"type", "text_delta"}, {"text", text}}));
}

} // namespace

TEST(ClaudeMessage, InitialState)
{
    ClaudeMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
    EXPECT_TRUE(msg.currentRedactedThinkingContent().isEmpty());
}

TEST(ClaudeMessage, MessageStartCarriesUsageAndResetsTheTurn)
{
    ClaudeMessage msg;
    startText(msg, 0, "stale");
    msg.applyEvent(messageDelta("end_turn"));
    ASSERT_EQ(msg.state(), MessageState::Final);

    const MessageEffects effects = msg.applyEvent(messageStart());

    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_EQ(effects.usage["usage"].toObject()["input_tokens"].toInt(), 12)
        << "message_start hands the client the message envelope, not the event";
}

TEST(ClaudeMessage, TextDeltasAccumulateAndAreHandedBackAsChunks)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, textBlock()));

    const MessageEffects first = msg.applyEvent(
        blockDelta(0, QJsonObject{{"type", "text_delta"}, {"text", "Hello "}}));
    const MessageEffects second = msg.applyEvent(
        blockDelta(0, QJsonObject{{"type", "text_delta"}, {"text", "world"}}));

    EXPECT_EQ(first.chunk, "Hello ");
    EXPECT_EQ(second.chunk, "world");

    ASSERT_EQ(msg.currentBlocks().size(), 1);
    const auto *textBlock = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text, "Hello world");
}

TEST(ClaudeMessage, NonTextDeltasProduceNoChunk)
{
    ClaudeMessage msg;
    msg.applyEvent(
        blockStart(0, QJsonObject{{"type", "thinking"}, {"thinking", ""}, {"signature", ""}}));

    const MessageEffects effects = msg.applyEvent(
        blockDelta(0, QJsonObject{{"type", "thinking_delta"}, {"thinking", "hm"}}));

    EXPECT_TRUE(effects.chunk.isEmpty()) << "thinking never reaches chunkReceived";
}

TEST(ClaudeMessage, ToolUseBlockStartsFromTheWireEvent)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, toolUseBlock("tool-123", "read_file")));

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_EQ(msg.currentToolUseContent()[0].id, "tool-123");
    EXPECT_EQ(msg.currentToolUseContent()[0].name, "read_file");
}

TEST(ClaudeMessage, StreamedToolInputIsParsedOnBlockStop)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, toolUseBlock("tool-1", "write")));
    msg.applyEvent(
        blockDelta(0, QJsonObject{{"type", "input_json_delta"}, {"partial_json", R"({"path":)"}}));
    msg.applyEvent(
        blockDelta(0, QJsonObject{{"type", "input_json_delta"}, {"partial_json", R"("/tmp/f"})"}}));
    msg.applyEvent(blockStop(0));

    EXPECT_EQ(msg.currentToolUseContent()[0].input["path"].toString(), "/tmp/f");
}

TEST(ClaudeMessage, UnparseableStreamedToolInputYieldsAnEmptyObject)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, toolUseBlock("tool-1", "write")));
    msg.applyEvent(
        blockDelta(0, QJsonObject{{"type", "input_json_delta"}, {"partial_json", R"({"path":)"}}));
    msg.applyEvent(blockStop(0));

    EXPECT_TRUE(msg.currentToolUseContent()[0].input.isEmpty());
}

TEST(ClaudeMessage, ThinkingBlockCollectsTextAndSignature)
{
    ClaudeMessage msg;
    msg.applyEvent(
        blockStart(0, QJsonObject{{"type", "thinking"}, {"thinking", ""}, {"signature", ""}}));
    msg.applyEvent(
        blockDelta(0, QJsonObject{{"type", "thinking_delta"}, {"thinking", "Let me think..."}}));
    msg.applyEvent(blockDelta(0, QJsonObject{{"type", "signature_delta"}, {"signature", "sig123"}}));

    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].thinking, "Let me think...");
    EXPECT_EQ(msg.currentThinkingContent()[0].signature, "sig123");
}

TEST(ClaudeMessage, RedactedThinkingBlockKeepsItsSignature)
{
    ClaudeMessage msg;
    msg.applyEvent(
        blockStart(0, QJsonObject{{"type", "redacted_thinking"}, {"signature", "redacted-sig"}}));

    ASSERT_EQ(msg.currentRedactedThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentRedactedThinkingContent()[0].signature, "redacted-sig");
}

TEST(ClaudeMessage, BlockStopAsksTheClientToFlushThinkingNotifications)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, textBlock()));

    EXPECT_TRUE(msg.applyEvent(blockStop(0)).thinkingCompleted);
}

TEST(ClaudeMessage, StopReasonEndTurnIsFinal)
{
    ClaudeMessage msg;
    startText(msg, 0, "hi");

    const MessageEffects effects = msg.applyEvent(messageDelta("end_turn"));

    EXPECT_EQ(msg.state(), MessageState::Final);
    EXPECT_EQ(msg.stopReason(), "end_turn");
    EXPECT_TRUE(effects.toolsReady) << "the client dispatches on the message state, not on a flag";
    EXPECT_EQ(effects.usage["usage"].toObject()["output_tokens"].toInt(), 42);
}

TEST(ClaudeMessage, StopReasonToolUseRequiresExecution)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, toolUseBlock("t1", "tool")));
    msg.applyEvent(messageDelta("tool_use"));

    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(ClaudeMessage, StopReasonToolUseWithoutToolBlocksIsMerelyComplete)
{
    ClaudeMessage msg;
    startText(msg, 0, "hi");
    msg.applyEvent(messageDelta("tool_use"));

    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(ClaudeMessage, StopReasonMaxTokensIsComplete)
{
    ClaudeMessage msg;
    msg.applyEvent(messageDelta("max_tokens"));

    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(ClaudeMessage, MessageDeltaWithoutAStopReasonStillCarriesUsage)
{
    ClaudeMessage msg;
    const MessageEffects effects = msg.applyEvent(
        QJsonObject{
            {"type", "message_delta"},
            {"delta", QJsonObject{}},
            {"usage", QJsonObject{{"output_tokens", 7}}}});

    EXPECT_FALSE(effects.toolsReady);
    EXPECT_EQ(effects.usage["usage"].toObject()["output_tokens"].toInt(), 7);
    EXPECT_EQ(msg.state(), MessageState::Building);
}

TEST(ClaudeMessage, UnknownEventTypesAreIgnored)
{
    ClaudeMessage msg;
    const MessageEffects effects = msg.applyEvent(QJsonObject{{"type", "ping"}});

    EXPECT_TRUE(effects.chunk.isEmpty());
    EXPECT_TRUE(effects.usage.isEmpty());
    EXPECT_FALSE(effects.thinkingCompleted);
    EXPECT_FALSE(effects.toolsReady);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(ClaudeMessage, DeltaForAnUnopenedBlockIsDropped)
{
    ClaudeMessage msg;
    msg.applyEvent(blockDelta(99, QJsonObject{{"type", "text_delta"}, {"text", "orphan"}}));

    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(ClaudeMessage, BlockStopForABlockWithoutToolInputIsHarmless)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, textBlock()));
    msg.applyEvent(blockStop(0));

    EXPECT_EQ(msg.currentBlocks().size(), 1);
}

TEST(ClaudeMessage, BufferedResponseCollectsTextIntoOneChunk)
{
    ClaudeMessage msg;

    const MessageEffects effects = msg.applyResponse(
        QJsonObject{
            {"content", QJsonArray{textBlock("Hello "), textBlock("world")}},
            {"stop_reason", "end_turn"},
            {"usage", QJsonObject{{"input_tokens", 3}, {"output_tokens", 5}}}});

    EXPECT_EQ(effects.chunk, "Hello world");
    EXPECT_TRUE(effects.toolsReady);
    EXPECT_EQ(effects.usage["usage"].toObject()["output_tokens"].toInt(), 5);
    EXPECT_EQ(msg.state(), MessageState::Final);
    ASSERT_EQ(msg.currentBlocks().size(), 2);
}

TEST(ClaudeMessage, BufferedToolUseKeepsItsCompleteInput)
{
    ClaudeMessage msg;

    msg.applyResponse(
        QJsonObject{
            {"content",
             QJsonArray{toolUseBlock("t1", "get_weather", QJsonObject{{"city", "Berlin"}})}},
            {"stop_reason", "tool_use"}});

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_EQ(msg.currentToolUseContent()[0].input["city"].toString(), "Berlin")
        << "a buffered tool_use arrives complete -- there are no input_json_deltas to wait for";
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(ClaudeMessage, BufferedThinkingIsNotReplayedTwice)
{
    ClaudeMessage msg;

    const MessageEffects effects = msg.applyResponse(
        QJsonObject{
            {"content",
             QJsonArray{
                 QJsonObject{{"type", "thinking"}, {"thinking", "step one"}, {"signature", "sig"}},
                 textBlock("answer")}},
            {"stop_reason", "end_turn"}});

    EXPECT_TRUE(effects.thinkingCompleted);
    EXPECT_EQ(effects.chunk, "answer");
    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].thinking, "step one")
        << "the complete block already carries the text; replaying it would double it";
    EXPECT_EQ(msg.currentThinkingContent()[0].signature, "sig");
}

TEST(ClaudeMessage, BufferedResponseWithoutAStopReasonLeavesTheTurnOpen)
{
    ClaudeMessage msg;

    const MessageEffects effects = msg.applyResponse(
        QJsonObject{{"content", QJsonArray{textBlock("partial")}}});

    EXPECT_FALSE(effects.toolsReady);
    EXPECT_EQ(msg.state(), MessageState::Building);
}

TEST(ClaudeMessage, BufferedResponseStartsANewTurn)
{
    ClaudeMessage msg;
    startText(msg, 0, "previous");
    msg.applyEvent(messageDelta("tool_use"));

    msg.applyResponse(
        QJsonObject{{"content", QJsonArray{textBlock("fresh")}}, {"stop_reason", "end_turn"}});

    ASSERT_EQ(msg.currentBlocks().size(), 1);
    const auto *block = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->text, "fresh");
}

TEST(ClaudeMessage, ToProviderFormat_TextOnly)
{
    ClaudeMessage msg;
    startText(msg, 0, "Hello");

    const QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    const QJsonArray content = result["content"].toArray();
    ASSERT_EQ(content.size(), 1);
    EXPECT_EQ(content[0].toObject()["type"].toString(), "text");
    EXPECT_EQ(content[0].toObject()["text"].toString(), "Hello");
}

TEST(ClaudeMessage, ToProviderFormat_MixedBlocks)
{
    ClaudeMessage msg;
    msg.applyEvent(
        blockStart(0, QJsonObject{{"type", "thinking"}, {"thinking", ""}, {"signature", ""}}));
    msg.applyEvent(blockDelta(0, QJsonObject{{"type", "thinking_delta"}, {"thinking", "hmm"}}));
    startText(msg, 1, "answer");

    const QJsonArray content = msg.toProviderFormat()["content"].toArray();
    ASSERT_EQ(content.size(), 2);
    EXPECT_EQ(content[0].toObject()["type"].toString(), "thinking");
    EXPECT_EQ(content[1].toObject()["type"].toString(), "text");
}

TEST(ClaudeMessage, CreateToolResultsContent)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, toolUseBlock("t1", "read")));
    msg.applyEvent(blockStart(1, toolUseBlock("t2", "write")));

    QHash<QString, ToolResult> results;
    results["t1"] = ToolResult::text("file content");
    results["t2"] = ToolResult::text("write ok");

    const QJsonArray toolResults = msg.createToolResultsContent(results);
    ASSERT_EQ(toolResults.size(), 2);

    bool foundT1 = false, foundT2 = false;
    for (const auto &val : toolResults) {
        const QJsonObject obj = val.toObject();
        EXPECT_EQ(obj["type"].toString(), "tool_result");
        // Single-text-block fast path: content is a bare string.
        if (obj["tool_use_id"].toString() == "t1") {
            EXPECT_EQ(obj["content"].toString(), "file content");
            foundT1 = true;
        }
        if (obj["tool_use_id"].toString() == "t2") {
            EXPECT_EQ(obj["content"].toString(), "write ok");
            foundT2 = true;
        }
    }
    EXPECT_TRUE(foundT1);
    EXPECT_TRUE(foundT2);
}

TEST(ClaudeMessage, CreateToolResultsContentWithImageBlock)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, toolUseBlock("img", "read_image")));

    // A rich result: a description + an image block. Claude should emit a
    // tool_result whose `content` is a JSON array (not a bare string),
    // containing a text block and an image block with base64-encoded data.
    ToolResult r;
    r.content.append(TextContent{"here is the screenshot"});
    const QByteArray pngBytes = QByteArray(
        "\x89PNG\r\n\x1a\n"
        "fake",
        12);
    r.content.append(ImageContent::fromBytes(pngBytes, "image/png"));

    QHash<QString, ToolResult> results;
    results["img"] = r;

    const QJsonArray toolResults = msg.createToolResultsContent(results);
    ASSERT_EQ(toolResults.size(), 1);

    const QJsonObject wrap = toolResults.first().toObject();
    EXPECT_EQ(wrap["type"].toString(), "tool_result");
    EXPECT_EQ(wrap["tool_use_id"].toString(), "img");
    ASSERT_TRUE(wrap["content"].isArray());

    const QJsonArray content = wrap["content"].toArray();
    ASSERT_EQ(content.size(), 2);

    EXPECT_EQ(content[0].toObject()["type"].toString(), "text");
    EXPECT_EQ(content[0].toObject()["text"].toString(), "here is the screenshot");

    const QJsonObject imgBlock = content[1].toObject();
    EXPECT_EQ(imgBlock["type"].toString(), "image");
    const QJsonObject source = imgBlock["source"].toObject();
    EXPECT_EQ(source["type"].toString(), "base64");
    EXPECT_EQ(source["media_type"].toString(), "image/png");
    EXPECT_EQ(QByteArray::fromBase64(source["data"].toString().toUtf8()), pngBytes);
}

TEST(ClaudeMessage, CreateToolResultsContentMarksErrors)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, toolUseBlock("err", "broken")));

    QHash<QString, ToolResult> results;
    results["err"] = ToolResult::error("nope");

    const QJsonArray toolResults = msg.createToolResultsContent(results);
    ASSERT_EQ(toolResults.size(), 1);
    const QJsonObject wrap = toolResults.first().toObject();
    EXPECT_EQ(wrap["content"].toString(), "nope");
    EXPECT_TRUE(wrap["is_error"].toBool());
}

TEST(ClaudeMessage, StartNewContinuation)
{
    ClaudeMessage msg;
    startText(msg, 0, "old");
    msg.applyEvent(messageDelta("end_turn"));
    EXPECT_EQ(msg.state(), MessageState::Final);

    msg.startNewContinuation();
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.stopReason().isEmpty());
}

TEST(ClaudeMessage, HandleImageBlock)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, base64ImageBlock("abc", "image/png")));

    ASSERT_EQ(msg.currentBlocks().size(), 1);
    const auto *imgBlock = std::get_if<ImageContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(imgBlock, nullptr);
    EXPECT_EQ(
        imgBlock->base64(), QString::fromUtf8(QByteArray::fromBase64(QByteArray("abc")).toBase64()));
    EXPECT_EQ(imgBlock->mimeType, "image/png");
    EXPECT_FALSE(imgBlock->isUrl());
}

TEST(ClaudeMessage, HandleImageBlock_Url)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(
        0,
        QJsonObject{
            {"type", "image"},
            {"source", QJsonObject{{"type", "url"}, {"url", "https://example.com/photo.jpg"}}}}));

    ASSERT_EQ(msg.currentBlocks().size(), 1);
    const auto *imgBlock = std::get_if<ImageContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(imgBlock, nullptr);
    EXPECT_EQ(imgBlock->url().toString(), "https://example.com/photo.jpg");
    EXPECT_TRUE(imgBlock->isUrl());
}

TEST(ClaudeMessage, HandleImageBlock_JpegMediaType)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, base64ImageBlock("jpegbytes", "image/jpeg")));

    const auto *imgBlock = std::get_if<ImageContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(imgBlock, nullptr);
    EXPECT_EQ(imgBlock->mimeType, "image/jpeg");
}

TEST(ClaudeMessage, ToProviderFormat_WithImage)
{
    ClaudeMessage msg;
    startText(msg, 0, "Here is an image:");

    const QString base64 = QString::fromUtf8(QByteArray("imgdata").toBase64());
    msg.applyEvent(blockStart(1, base64ImageBlock(base64, "image/png")));

    const QJsonArray content = msg.toProviderFormat()["content"].toArray();
    ASSERT_EQ(content.size(), 2);
    EXPECT_EQ(content[0].toObject()["type"].toString(), "text");
    EXPECT_EQ(content[1].toObject()["type"].toString(), "image");
    EXPECT_EQ(content[1].toObject()["source"].toObject()["data"].toString(), base64);
}

TEST(ClaudeMessage, ToProviderFormat_MultipleImages)
{
    ClaudeMessage msg;
    msg.applyEvent(blockStart(0, base64ImageBlock("img1", "image/png")));
    msg.applyEvent(blockStart(
        1,
        QJsonObject{
            {"type", "image"},
            {"source", QJsonObject{{"type", "url"}, {"url", "https://example.com/img.jpg"}}}}));

    const QJsonArray content = msg.toProviderFormat()["content"].toArray();
    ASSERT_EQ(content.size(), 2);
    EXPECT_EQ(content[0].toObject()["source"].toObject()["type"].toString(), "base64");
    EXPECT_EQ(content[1].toObject()["source"].toObject()["type"].toString(), "url");
}

TEST(ClaudeMessage, HandleMixedContent_TextImageToolUse)
{
    ClaudeMessage msg;
    startText(msg, 0, "Look at this:");
    msg.applyEvent(blockStart(1, base64ImageBlock("pic", "image/webp")));
    msg.applyEvent(blockStart(2, toolUseBlock("t1", "analyze")));

    ASSERT_EQ(msg.currentBlocks().size(), 3);
    EXPECT_TRUE(std::holds_alternative<TextContent>(msg.currentBlocks()[0]));
    EXPECT_TRUE(std::holds_alternative<ImageContent>(msg.currentBlocks()[1]));
    EXPECT_TRUE(std::holds_alternative<ToolUseContent>(msg.currentBlocks()[2]));
}

TEST(ClaudeMessage, AToolUseAfterAnUnknownBlockKeepsItsArguments)
{
    ClaudeMessage msg;
    msg.applyEvent(messageStart());
    msg.applyEvent(blockStart(
        0,
        QJsonObject{
            {"type", "server_tool_use"},
            {"id", "srvtoolu_1"},
            {"name", "web_search"},
            {"input", QJsonObject{}}}));
    msg.applyEvent(blockStop(0));

    msg.applyEvent(blockStart(1, toolUseBlock("toolu_1", "echo")));
    msg.applyEvent(
        blockDelta(1, QJsonObject{{"type", "input_json_delta"}, {"partial_json", R"({"value":)"}}));
    msg.applyEvent(
        blockDelta(1, QJsonObject{{"type", "input_json_delta"}, {"partial_json", R"("7"})"}}));
    msg.applyEvent(blockStop(1));
    msg.applyEvent(messageDelta("tool_use"));

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_EQ(msg.currentToolUseContent().first().input.value("value").toString(), "7")
        << "the wire index runs ahead of the block list once a block is skipped";
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(ClaudeMessage, TextAfterAnUnknownBlockLandsInItsOwnBlock)
{
    ClaudeMessage msg;
    msg.applyEvent(messageStart());
    msg.applyEvent(blockStart(0, QJsonObject{{"type", "web_search_tool_result"}}));
    msg.applyEvent(blockStop(0));
    startText(msg, 1, "found it");

    ASSERT_EQ(msg.currentBlocks().size(), 1);
    auto *text = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "found it");
}

TEST(ClaudeMessage, AnErrorEventIsHandedBackAsAnError)
{
    ClaudeMessage msg;
    const MessageEffects effects = msg.applyEvent(QJsonObject{
        {"type", "error"},
        {"error", QJsonObject{{"type", "overloaded_error"}, {"message", "Overloaded"}}}});

    ASSERT_TRUE(effects.error.has_value());
    EXPECT_EQ(effects.error->value("message").toString(), "Overloaded");
    EXPECT_EQ(effects.error->value("type").toString(), "overloaded_error");
}
