// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "clients/openai/OpenAIResponsesMessage.hpp"

using namespace LLMQore;

namespace {

MessageEffects textDelta(OpenAIResponsesMessage &msg, const QString &delta)
{
    return msg
        .applyEvent(QStringLiteral("response.output_text.delta"), QJsonObject{{"delta", delta}});
}

QJsonObject functionCallItem(
    const QString &callId,
    const QString &name,
    const QString &itemId = {},
    const QString &arguments = {})
{
    QJsonObject item{{"type", "function_call"}, {"call_id", callId}, {"name", name}};
    if (!itemId.isEmpty())
        item.insert("id", itemId);
    if (!arguments.isEmpty())
        item.insert("arguments", arguments);
    return item;
}

void startToolCall(
    OpenAIResponsesMessage &msg, const QString &callId, const QString &name, const QString &itemId)
{
    msg.applyEvent(
        QStringLiteral("response.output_item.added"),
        QJsonObject{{"item", functionCallItem(callId, name, itemId)}});
}

void toolArgumentsDelta(OpenAIResponsesMessage &msg, const QString &itemId, const QString &delta)
{
    msg.applyEvent(
        QStringLiteral("response.function_call_arguments.delta"),
        QJsonObject{{"item_id", itemId}, {"delta", delta}});
}

void toolArgumentsDone(
    OpenAIResponsesMessage &msg, const QString &itemId, const QString &arguments = {})
{
    QJsonObject data{{"item_id", itemId}};
    if (!arguments.isEmpty())
        data.insert("arguments", arguments);
    msg.applyEvent(QStringLiteral("response.function_call_arguments.done"), data);
}

void reasoningDelta(OpenAIResponsesMessage &msg, const QString &itemId, const QString &text)
{
    msg.applyEvent(
        QStringLiteral("response.reasoning_content.delta"),
        QJsonObject{{"item_id", itemId}, {"delta", text}});
}

MessageEffects completed(OpenAIResponsesMessage &msg, const QJsonObject &response)
{
    return msg.applyEvent(QStringLiteral("response.completed"), QJsonObject{{"response", response}});
}

QJsonObject messageItem(const QString &text)
{
    return QJsonObject{
        {"type", "message"},
        {"role", "assistant"},
        {"content", QJsonArray{QJsonObject{{"type", "output_text"}, {"text", text}}}}};
}

} // namespace

TEST(OpenAIResponsesMessage, InitialState)
{
    OpenAIResponsesMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.accumulatedText().isEmpty());
    EXPECT_FALSE(msg.hasToolCalls());
    EXPECT_FALSE(msg.hasThinkingContent());
}

TEST(OpenAIResponsesMessage, TextDeltasAccumulateAndAreHandedBackAsChunks)
{
    OpenAIResponsesMessage msg;

    EXPECT_EQ(textDelta(msg, "Hello ").chunk, "Hello ");
    EXPECT_EQ(textDelta(msg, "world").chunk, "world");

    EXPECT_EQ(msg.accumulatedText(), "Hello world");
    EXPECT_EQ(msg.currentBlocks().size(), 1);
}

TEST(OpenAIResponsesMessage, EmptyTextDeltaCreatesNothing)
{
    OpenAIResponsesMessage msg;

    EXPECT_TRUE(textDelta(msg, QString()).chunk.isEmpty());
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.accumulatedText().isEmpty());
}

TEST(OpenAIResponsesMessage, OutputTextDoneReplacesTheAccumulatedText)
{
    OpenAIResponsesMessage msg;
    textDelta(msg, "par");

    const MessageEffects effects = msg.applyEvent(
        QStringLiteral("response.output_text.done"), QJsonObject{{"text", "partial answer"}});

    EXPECT_EQ(effects.fullText, "partial answer");
    EXPECT_TRUE(effects.chunk.isEmpty()) << "the done event is a correction, not a new chunk";
}

TEST(OpenAIResponsesMessage, OutputItemAddedOpensAFunctionCall)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_abc", "read_file", "item_1");

    EXPECT_TRUE(msg.hasToolCalls());
    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_EQ(msg.currentToolUseContent()[0].id, "call_abc");
    EXPECT_EQ(msg.currentToolUseContent()[0].name, "read_file");
}

TEST(OpenAIResponsesMessage, StreamedArgumentsAreCorrelatedByItemIdAndParsedOnDone)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "write", "item_1");
    toolArgumentsDelta(msg, "item_1", R"({"path":)");
    toolArgumentsDelta(msg, "item_1", R"("/tmp/f"})");
    toolArgumentsDone(msg, "item_1");

    EXPECT_EQ(msg.currentToolUseContent()[0].input["path"].toString(), "/tmp/f")
        << "the item -> call correlation table lives in the translator, keyed by item_id";
}

TEST(OpenAIResponsesMessage, ArgumentsForAnUnknownItemIdAreDropped)
{
    OpenAIResponsesMessage msg;
    toolArgumentsDelta(msg, "unknown_item", R"({"key":"value"})");

    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
}

TEST(OpenAIResponsesMessage, ArgumentsDoneCarriesTheFinalStringWhenNoDeltasArrived)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "write", "item_1");
    toolArgumentsDone(msg, "item_1", R"({"path":"/etc"})");

    EXPECT_EQ(msg.currentToolUseContent()[0].input["path"].toString(), "/etc");
}

TEST(OpenAIResponsesMessage, ToolCallWithoutArgumentsEndsUpEmpty)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "no_args", "item_1");
    toolArgumentsDone(msg, "item_1");

    EXPECT_TRUE(msg.currentToolUseContent()[0].input.isEmpty());
}

TEST(OpenAIResponsesMessage, UnparseableArgumentsEndUpEmpty)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "tool", "item_1");
    toolArgumentsDelta(msg, "item_1", "not valid json");
    toolArgumentsDone(msg, "item_1");

    EXPECT_TRUE(msg.currentToolUseContent()[0].input.isEmpty());
}

TEST(OpenAIResponsesMessage, OutputItemDoneClosesAFunctionCallByItsOwnItem)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "search", "item_1");

    msg.applyEvent(
        QStringLiteral("response.output_item.done"),
        QJsonObject{{"item", functionCallItem("call_1", "search", "item_1", R"({"q":"qt"})")}});

    EXPECT_EQ(msg.currentToolUseContent()[0].input["q"].toString(), "qt");
}

TEST(OpenAIResponsesMessage, OutputItemAddedOpensAReasoningBlock)
{
    OpenAIResponsesMessage msg;
    msg.applyEvent(
        QStringLiteral("response.output_item.added"),
        QJsonObject{{"item", QJsonObject{{"type", "reasoning"}, {"id", "item_1"}}}});

    EXPECT_TRUE(msg.hasThinkingContent());
    EXPECT_EQ(msg.currentThinkingContent().size(), 1);
}

TEST(OpenAIResponsesMessage, ReasoningDeltasAccumulate)
{
    OpenAIResponsesMessage msg;
    msg.applyEvent(
        QStringLiteral("response.output_item.added"),
        QJsonObject{{"item", QJsonObject{{"type", "reasoning"}, {"id", "item_1"}}}});
    reasoningDelta(msg, "item_1", "Let me think...");
    reasoningDelta(msg, "item_1", " More thinking.");

    EXPECT_EQ(msg.currentThinkingContent()[0].thinking, "Let me think... More thinking.");
}

TEST(OpenAIResponsesMessage, ReasoningDeltaForAnUnknownItemIdCreatesTheBlock)
{
    OpenAIResponsesMessage msg;
    reasoningDelta(msg, "unknown", "arrived without an output_item.added");

    const auto thinking = msg.currentThinkingContent();
    ASSERT_EQ(thinking.size(), 1);
    EXPECT_EQ(thinking.first().itemId, "unknown");
    EXPECT_EQ(thinking.first().thinking, "arrived without an output_item.added");
}

TEST(OpenAIResponsesMessage, EmptyReasoningDeltaCreatesNothing)
{
    OpenAIResponsesMessage msg;
    reasoningDelta(msg, "unknown", QString());

    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
}

TEST(OpenAIResponsesMessage, ReasoningDoneAsksTheClientToFlushThinkingNotifications)
{
    OpenAIResponsesMessage msg;
    reasoningDelta(msg, "item_1", "thinking...");

    const MessageEffects effects = msg.applyEvent(
        QStringLiteral("response.reasoning_content.done"), QJsonObject{{"item_id", "item_1"}});

    EXPECT_TRUE(effects.thinkingCompleted);
    EXPECT_EQ(msg.currentThinkingContent().size(), 1);
}

TEST(OpenAIResponsesMessage, StreamedReasoningItemDoneSubstitutesAPlaceholder)
{
    OpenAIResponsesMessage msg;

    const MessageEffects effects = msg.applyEvent(
        QStringLiteral("response.output_item.done"),
        QJsonObject{
            {"item_id", "item_1"},
            {"item",
             QJsonObject{{"type", "reasoning"}, {"id", "item_1"}, {"encrypted_content", "blob"}}}});

    EXPECT_TRUE(effects.thinkingCompleted);
    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_TRUE(msg.currentThinkingContent()[0].thinking.contains("streaming mode"));
    EXPECT_EQ(msg.currentThinkingContent()[0].encryptedContent, "blob");
}

TEST(OpenAIResponsesMessage, ReasoningItemPrefersItsSummaryOverItsContent)
{
    OpenAIResponsesMessage msg;

    msg.applyEvent(
        QStringLiteral("response.output_item.done"),
        QJsonObject{
            {"item_id", "item_1"},
            {"item",
             QJsonObject{
                 {"type", "reasoning"},
                 {"id", "item_1"},
                 {"summary",
                  QJsonArray{QJsonObject{{"type", "summary_text"}, {"text", "the summary"}}}},
                 {"content",
                  QJsonArray{QJsonObject{{"type", "reasoning_text"}, {"text", "the raw trace"}}}}}}});

    EXPECT_EQ(msg.currentThinkingContent()[0].thinking, "the summary");
}

TEST(OpenAIResponsesMessage, TerminalCompletedWithToolsRequiresExecution)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "tool", "item_1");
    toolArgumentsDone(msg, "item_1");

    const MessageEffects effects
        = completed(msg, QJsonObject{{"status", "completed"}, {"output", QJsonArray{}}});

    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
    EXPECT_TRUE(effects.toolsReady);
    EXPECT_TRUE(effects.thinkingCompleted);
}

TEST(OpenAIResponsesMessage, TerminalCompletedWithoutToolsIsComplete)
{
    OpenAIResponsesMessage msg;
    textDelta(msg, "answer");

    completed(msg, QJsonObject{{"status", "completed"}});

    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(OpenAIResponsesMessage, TerminalAggregatesTextOnlyAsAFallback)
{
    OpenAIResponsesMessage msg;

    const MessageEffects effects = completed(
        msg,
        QJsonObject{
            {"status", "completed"},
            {"output", QJsonArray{messageItem("first "), messageItem("second")}}});

    EXPECT_EQ(effects.fallbackText, "first second");
    EXPECT_TRUE(effects.fullText.isEmpty())
        << "an aggregate never overwrites text the client already streamed";
}

TEST(OpenAIResponsesMessage, TerminalPrefersTheOutputTextShortcut)
{
    OpenAIResponsesMessage msg;

    const MessageEffects effects = completed(
        msg,
        QJsonObject{
            {"status", "completed"},
            {"output_text", "short cut"},
            {"output", QJsonArray{messageItem("ignored")}}});

    EXPECT_EQ(effects.fallbackText, "short cut");
}

TEST(OpenAIResponsesMessage, TerminalCarriesTheResponseObjectAsUsage)
{
    OpenAIResponsesMessage msg;

    const MessageEffects effects = completed(
        msg,
        QJsonObject{
            {"status", "completed"},
            {"usage", QJsonObject{{"input_tokens", 11}, {"output_tokens", 22}}}});

    EXPECT_EQ(effects.usage["usage"].toObject()["output_tokens"].toInt(), 22);
}

TEST(OpenAIResponsesMessage, IncompleteIsFinal)
{
    OpenAIResponsesMessage msg;

    const MessageEffects effects = msg.applyEvent(
        QStringLiteral("response.incomplete"),
        QJsonObject{{"response", QJsonObject{{"status", "incomplete"}}}});

    EXPECT_EQ(msg.state(), MessageState::Final);
    EXPECT_TRUE(effects.toolsReady);
}

TEST(OpenAIResponsesMessage, IncompleteWithoutAResponseObjectStillLandsOnAStatus)
{
    OpenAIResponsesMessage msg;

    msg.applyEvent(QStringLiteral("response.incomplete"), QJsonObject{});

    EXPECT_EQ(msg.state(), MessageState::Final);
    EXPECT_EQ(msg.stopReason(), "incomplete");
}

TEST(OpenAIResponsesMessage, StatusInProgressKeepsBuilding)
{
    OpenAIResponsesMessage msg;
    completed(msg, QJsonObject{{"status", "in_progress"}});

    EXPECT_EQ(msg.state(), MessageState::Building);
}

TEST(OpenAIResponsesMessage, StatusFailedIsFinal)
{
    OpenAIResponsesMessage msg;
    completed(msg, QJsonObject{{"status", "failed"}});

    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OpenAIResponsesMessage, StatusCancelledIsFinal)
{
    OpenAIResponsesMessage msg;
    completed(msg, QJsonObject{{"status", "cancelled"}});

    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OpenAIResponsesMessage, UnknownStatusKeepsBuilding)
{
    OpenAIResponsesMessage msg;
    completed(msg, QJsonObject{{"status", "something_new"}});

    EXPECT_EQ(msg.state(), MessageState::Building);
}

TEST(OpenAIResponsesMessage, UnknownEventTypesAreIgnored)
{
    OpenAIResponsesMessage msg;

    const MessageEffects effects
        = msg.applyEvent(QStringLiteral("response.audio.delta"), QJsonObject{{"delta", "x"}});

    EXPECT_TRUE(effects.chunk.isEmpty());
    EXPECT_FALSE(effects.toolsReady);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

// --- the buffered path replays whole output items through the same code ---

TEST(OpenAIResponsesMessage, BufferedResponseCollectsMessageText)
{
    OpenAIResponsesMessage msg;

    const MessageEffects effects = msg.applyResponse(
        QJsonObject{
            {"status", "completed"},
            {"output", QJsonArray{messageItem("Hello "), messageItem("world")}},
            {"usage", QJsonObject{{"output_tokens", 5}}}});

    EXPECT_EQ(effects.chunk, "Hello world");
    EXPECT_EQ(msg.accumulatedText(), "Hello world");
    EXPECT_TRUE(effects.toolsReady);
    EXPECT_EQ(effects.usage["usage"].toObject()["output_tokens"].toInt(), 5);
    EXPECT_EQ(msg.state(), MessageState::Complete);
}

TEST(OpenAIResponsesMessage, BufferedFunctionCallKeepsItsCompleteArguments)
{
    OpenAIResponsesMessage msg;

    msg.applyResponse(
        QJsonObject{
            {"status", "completed"},
            {"output",
             QJsonArray{
                 functionCallItem("call_1", "get_weather", "item_1", R"({"city":"Berlin"})")}}});

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_EQ(msg.currentToolUseContent()[0].input["city"].toString(), "Berlin");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OpenAIResponsesMessage, BufferedReasoningKeepsItsContinuationToken)
{
    OpenAIResponsesMessage msg;

    msg.applyResponse(
        QJsonObject{
            {"status", "completed"},
            {"output",
             QJsonArray{QJsonObject{
                 {"type", "reasoning"},
                 {"id", "item_1"},
                 {"encrypted_content", "opaque"},
                 {"summary",
                  QJsonArray{QJsonObject{{"type", "summary_text"}, {"text", "thought"}}}}}}}});

    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].thinking, "thought");
    EXPECT_EQ(msg.currentThinkingContent()[0].encryptedContent, "opaque")
        << "dropping the encrypted item degrades the next turn silently";
}

TEST(OpenAIResponsesMessage, BufferedReasoningWithNothingInItCreatesNoBlock)
{
    OpenAIResponsesMessage msg;

    msg.applyResponse(
        QJsonObject{
            {"status", "completed"},
            {"output", QJsonArray{QJsonObject{{"type", "reasoning"}, {"id", "item_1"}}}}});

    EXPECT_TRUE(msg.currentThinkingContent().isEmpty())
        << "the streaming placeholder must not leak into a buffered response";
}

TEST(OpenAIResponsesMessage, BufferedResponseWithoutAStatusLeavesTheTurnOpen)
{
    OpenAIResponsesMessage msg;

    const MessageEffects effects = msg.applyResponse(
        QJsonObject{{"output", QJsonArray{messageItem("partial")}}});

    EXPECT_FALSE(effects.toolsReady);
    EXPECT_EQ(msg.state(), MessageState::Building);
}

// --- serialization is unchanged by the dispatch move ---

TEST(OpenAIResponsesMessage, ToItemsFormat_TextOnly)
{
    OpenAIResponsesMessage msg;
    textDelta(msg, "Hello world");

    const auto items = msg.toItemsFormat(ReasoningPersistence::Off);
    ASSERT_EQ(items.size(), 1);
    EXPECT_EQ(items[0]["role"].toString(), "assistant");
    EXPECT_EQ(items[0]["content"].toString(), "Hello world");
}

TEST(OpenAIResponsesMessage, ToItemsFormat_ToolCallsOnly)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "read_file", "item_1");
    toolArgumentsDelta(msg, "item_1", R"({"path": "/tmp"})");
    toolArgumentsDone(msg, "item_1");

    const auto items = msg.toItemsFormat(ReasoningPersistence::Off);
    ASSERT_EQ(items.size(), 1);
    EXPECT_EQ(items[0]["type"].toString(), "function_call");
    EXPECT_EQ(items[0]["call_id"].toString(), "call_1");
    EXPECT_EQ(items[0]["name"].toString(), "read_file");
}

TEST(OpenAIResponsesMessage, ToItemsFormat_TextAndToolCalls)
{
    OpenAIResponsesMessage msg;
    textDelta(msg, "Let me help");
    startToolCall(msg, "call_1", "search", "item_1");
    toolArgumentsDelta(msg, "item_1", R"({"q": "test"})");
    toolArgumentsDone(msg, "item_1");

    const auto items = msg.toItemsFormat(ReasoningPersistence::Off);
    ASSERT_EQ(items.size(), 2);
    EXPECT_EQ(items[0]["role"].toString(), "assistant");
    EXPECT_EQ(items[1]["type"].toString(), "function_call");
}

TEST(OpenAIResponsesMessage, CreateToolResultItems)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "read", "item_1");
    startToolCall(msg, "call_2", "write", "item_2");

    QHash<QString, ToolResult> results;
    results["call_1"] = ToolResult::text("file content");
    results["call_2"] = ToolResult::text("write ok");

    const QJsonArray items = msg.createToolResultItems(results);
    ASSERT_EQ(items.size(), 2);

    bool foundCall1 = false, foundCall2 = false;
    for (const auto &val : items) {
        const QJsonObject obj = val.toObject();
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
    startToolCall(msg, "call_img", "screenshot", "item_1");

    ToolResult r;
    r.content.append(TextContent{"here is the screenshot"});
    r.content.append(ImageContent::fromBytes(QByteArray("PNGDATA"), "image/png"));

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
    const QString base64 = dataUri.mid(QString("data:image/png;base64,").size());
    EXPECT_EQ(QByteArray::fromBase64(base64.toUtf8()), QByteArray("PNGDATA"));
}

TEST(OpenAIResponsesMessage, CreateToolResultItems_TextOnlyStillUsesBareString)
{
    OpenAIResponsesMessage msg;
    startToolCall(msg, "call_1", "read", "item_1");

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
    startToolCall(msg, "call_aud", "record", "item_1");

    ToolResult r;
    r.content.append(AudioContent{QByteArray("WAVDATA"), "audio/wav"});

    QHash<QString, ToolResult> results;
    results["call_aud"] = r;

    const QJsonArray items = msg.createToolResultItems(results);
    ASSERT_EQ(items.size(), 1);
    ASSERT_TRUE(items[0].toObject()["output"].isArray());
    const QJsonObject block = items[0].toObject()["output"].toArray()[0].toObject();
    EXPECT_EQ(block["type"].toString(), "input_text");
    EXPECT_TRUE(block["text"].toString().contains("audio"));
}

TEST(OpenAIResponsesMessage, StartNewContinuationDropsTheCorrelationTable)
{
    OpenAIResponsesMessage msg;
    textDelta(msg, "old");
    startToolCall(msg, "call_1", "tool", "item_1");
    reasoningDelta(msg, "item_r", "thought");
    completed(msg, QJsonObject{{"status", "completed"}});

    msg.startNewContinuation();
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_FALSE(msg.hasToolCalls());
    EXPECT_FALSE(msg.hasThinkingContent());
    EXPECT_TRUE(msg.accumulatedText().isEmpty());

    // item_1 belonged to the previous turn: its arguments must not resurrect it.
    toolArgumentsDelta(msg, "item_1", R"({"stale":1})");
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
}

TEST(OpenAIResponsesMessage, MultipleReasoningBlocks)
{
    OpenAIResponsesMessage msg;
    reasoningDelta(msg, "item_1", "First thought");
    reasoningDelta(msg, "item_2", "Second thought");

    EXPECT_EQ(msg.currentThinkingContent().size(), 2);
}
