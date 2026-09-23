// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include "clients/ollama/OllamaMessage.hpp"

using namespace LLMQore;

namespace {

QJsonObject line(const QJsonObject &message, bool done = false)
{
    return QJsonObject{{"message", message}, {"done", done}};
}

QJsonObject contentLine(const QString &content)
{
    return line(QJsonObject{{"role", "assistant"}, {"content", content}});
}

QJsonObject thinkingLine(const QString &thinking)
{
    return line(QJsonObject{{"role", "assistant"}, {"content", ""}, {"thinking", thinking}});
}

QJsonObject toolCallLine(const QString &name, const QJsonValue &arguments)
{
    return line(QJsonObject{
        {"role", "assistant"},
        {"content", ""},
        {"tool_calls",
         QJsonArray{QJsonObject{
             {"function", QJsonObject{{"name", name}, {"arguments", arguments}}}}}}});
}

QJsonObject doneLine(const QString &reason = QStringLiteral("stop"))
{
    QJsonObject done = line(QJsonObject{{"role", "assistant"}, {"content", ""}}, true);
    done["done_reason"] = reason;
    return done;
}

} // namespace

TEST(OllamaMessage, InitialState)
{
    OllamaMessage msg;
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
}

TEST(OllamaMessage, PlainTextAccumulatesAndIsHandedBackAsChunks)
{
    OllamaMessage msg;
    const MessageEffects first = msg.applyEvent(contentLine("Hello "));
    const MessageEffects second = msg.applyEvent(contentLine("world"));

    EXPECT_EQ(first.chunk, "Hello ");
    EXPECT_EQ(second.chunk, "world");
    EXPECT_TRUE(first.thinkingCompleted);

    ASSERT_EQ(msg.currentBlocks().size(), 1);
    auto *textBlock = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text, "Hello world");
}

TEST(OllamaMessage, JsonLookingContentIsHeldBackAsAPossibleToolCall)
{
    OllamaMessage msg;
    const MessageEffects effects = msg.applyEvent(contentLine(R"({"name": "read")"));

    EXPECT_TRUE(effects.chunk.isEmpty());
    for (const TurnContent &block : msg.currentBlocks())
        EXPECT_EQ(std::get_if<TextContent>(&block), nullptr);
}

TEST(OllamaMessage, GenerateEndpointResponseIsText)
{
    OllamaMessage msg;
    const MessageEffects effects
        = msg.applyEvent(QJsonObject{{"response", "hi"}, {"done", false}});

    EXPECT_EQ(effects.chunk, "hi");
    ASSERT_EQ(msg.currentBlocks().size(), 1);
    EXPECT_EQ(std::get_if<TextContent>(&msg.currentBlocks()[0])->text, "hi");
}

TEST(OllamaMessage, StructuredToolCall)
{
    OllamaMessage msg;
    msg.applyEvent(toolCallLine("read_file", QJsonObject{{"path", "/tmp"}}));

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    const ToolUseContent tool = msg.currentToolUseContent()[0];
    EXPECT_EQ(tool.name, "read_file");
    EXPECT_EQ(tool.input["path"].toString(), "/tmp");
    EXPECT_TRUE(tool.id.startsWith("call_read_file_"));
}

TEST(OllamaMessage, SeveralStructuredToolCalls)
{
    OllamaMessage msg;
    msg.applyEvent(toolCallLine("tool_a", QJsonObject{{"x", 1}}));
    msg.applyEvent(toolCallLine("tool_b", QJsonObject{{"y", 2}}));

    EXPECT_EQ(msg.currentToolUseContent().size(), 2);
}

TEST(OllamaMessage, SameToolTwiceGetsDistinctIds)
{
    OllamaMessage msg;
    msg.applyEvent(toolCallLine("read_file", QJsonObject{{"path", "/a"}}));
    msg.applyEvent(toolCallLine("read_file", QJsonObject{{"path", "/b"}}));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
    ASSERT_EQ(tools.size(), 2);
    EXPECT_TRUE(tools[0].id.startsWith("call_read_file_"));
    EXPECT_TRUE(tools[1].id.startsWith("call_read_file_"));
    EXPECT_NE(tools[0].id, tools[1].id);
    EXPECT_EQ(tools[0].input["path"].toString(), "/a");
    EXPECT_EQ(tools[1].input["path"].toString(), "/b");
}

TEST(OllamaMessage, ToolCallIdsAreDistinctAcrossContinuations)
{
    OllamaMessage msg;
    msg.applyEvent(toolCallLine("tool", QJsonObject{}));
    msg.applyEvent(doneLine());
    const QString firstId = msg.currentToolUseContent()[0].id;

    msg.startNewContinuation();
    msg.applyEvent(toolCallLine("tool", QJsonObject{}));

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_NE(msg.currentToolUseContent()[0].id, firstId);
}

TEST(OllamaMessage, DoneParsesAToolCallWrittenAsContent)
{
    OllamaMessage msg;
    msg.applyEvent(
        contentLine(R"({"name": "read_file", "arguments": {"path": "/tmp/test.txt"}})"));
    msg.applyEvent(doneLine());

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    const ToolUseContent tool = msg.currentToolUseContent()[0];
    EXPECT_EQ(tool.name, "read_file");
    EXPECT_EQ(tool.input["path"].toString(), "/tmp/test.txt");
    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OllamaMessage, DoneParsesAToolCallWithStringArguments)
{
    OllamaMessage msg;
    msg.applyEvent(contentLine(R"({"name": "tool", "arguments": "{\"key\": \"value\"}"})"));
    msg.applyEvent(doneLine());

    ASSERT_EQ(msg.currentToolUseContent().size(), 1);
    EXPECT_EQ(msg.currentToolUseContent()[0].input["key"].toString(), "value");
}

TEST(OllamaMessage, DoneAfterPlainTextIsFinal)
{
    OllamaMessage msg;
    msg.applyEvent(contentLine("Just a normal answer"));
    msg.applyEvent(doneLine());

    EXPECT_EQ(msg.state(), MessageState::Final);
    EXPECT_EQ(msg.stopReason(), "stop");
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());

    auto *textBlock = std::get_if<TextContent>(&msg.currentBlocks()[0]);
    ASSERT_NE(textBlock, nullptr);
    EXPECT_EQ(textBlock->text, "Just a normal answer");
}

TEST(OllamaMessage, ALineThatIsNotDoneLeavesTheTurnOpen)
{
    OllamaMessage msg;
    const MessageEffects effects = msg.applyEvent(contentLine("partial"));

    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_FALSE(effects.toolsReady);
}

TEST(OllamaMessage, DoneHandsBackUsageAndReadiesTools)
{
    OllamaMessage msg;
    QJsonObject done = doneLine();
    done["prompt_eval_count"] = 3;
    done["eval_count"] = 5;

    const MessageEffects effects = msg.applyEvent(done);

    EXPECT_TRUE(effects.toolsReady);
    EXPECT_TRUE(effects.thinkingCompleted);
    EXPECT_EQ(effects.usage["eval_count"].toInt(), 5);
}

TEST(OllamaMessage, AnErrorLineIsHandedBackAsAnError)
{
    OllamaMessage msg;
    const MessageEffects effects = msg.applyEvent(QJsonObject{{"error", "boom"}});

    ASSERT_TRUE(effects.error.has_value());
    EXPECT_EQ(effects.error->value("message").toString(), "boom");
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
}

TEST(OllamaMessage, ToolCallJsonWithAnEmptyNameIsNotACall)
{
    OllamaMessage msg;
    msg.applyEvent(contentLine(R"({"name": "", "arguments": {}})"));
    msg.applyEvent(doneLine());

    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OllamaMessage, IncompleteToolCallJsonIsDiscarded)
{
    OllamaMessage msg;
    msg.applyEvent(contentLine(R"({"name": "tool", "arguments": )"));
    msg.applyEvent(doneLine());

    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
}

TEST(OllamaMessage, JsonWithoutToolFieldsIsNotACall)
{
    OllamaMessage msg;
    msg.applyEvent(contentLine(R"({"key": "value", "other": 123})"));
    msg.applyEvent(doneLine());

    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_EQ(msg.state(), MessageState::Final);
}

TEST(OllamaMessage, ThinkingDeltasShareOneBlock)
{
    OllamaMessage msg;
    const MessageEffects effects = msg.applyEvent(thinkingLine("Step 1..."));
    msg.applyEvent(thinkingLine(" Step 2..."));

    EXPECT_TRUE(effects.chunk.isEmpty());
    const QList<ThinkingContent> thinkingBlocks = msg.currentThinkingContent();
    ASSERT_EQ(thinkingBlocks.size(), 1);
    EXPECT_EQ(thinkingBlocks[0].thinking, "Step 1... Step 2...");
}

TEST(OllamaMessage, TopLevelThinkingIsReadToo)
{
    OllamaMessage msg;
    msg.applyEvent(QJsonObject{{"thinking", "hmm"}, {"done", false}});

    ASSERT_EQ(msg.currentThinkingContent().size(), 1);
    EXPECT_EQ(msg.currentThinkingContent()[0].thinking, "hmm");
}

TEST(OllamaMessage, DoneSignatureLandsOnTheThinkingBlock)
{
    OllamaMessage msg;
    msg.applyEvent(thinkingLine("thinking..."));
    QJsonObject done = doneLine();
    done["signature"] = "sig-abc";
    msg.applyEvent(done);

    EXPECT_EQ(msg.currentThinkingContent()[0].signature, "sig-abc");
}

TEST(OllamaMessage, DoneSignatureWithoutThinkingCreatesNothing)
{
    OllamaMessage msg;
    QJsonObject done = doneLine();
    done["signature"] = "sig";
    msg.applyEvent(done);

    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
}

TEST(OllamaMessage, ToProviderFormat_TextOnly)
{
    OllamaMessage msg;
    msg.applyEvent(contentLine("Hello world"));
    msg.applyEvent(doneLine());

    const QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    EXPECT_EQ(result["content"].toString(), "Hello world");
    EXPECT_FALSE(result.contains("tool_calls"));
}

TEST(OllamaMessage, ToProviderFormat_WithToolCalls)
{
    OllamaMessage msg;
    msg.applyEvent(toolCallLine("read", QJsonObject{{"p", "a"}}));

    const QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["role"].toString(), "assistant");
    const QJsonArray toolCalls = result["tool_calls"].toArray();
    ASSERT_EQ(toolCalls.size(), 1);
    EXPECT_EQ(toolCalls[0].toObject()["type"].toString(), "function");
}

TEST(OllamaMessage, ToProviderFormat_WithThinking)
{
    OllamaMessage msg;
    msg.applyEvent(thinkingLine("hmm..."));
    msg.applyEvent(contentLine("answer"));
    msg.applyEvent(doneLine());

    const QJsonObject result = msg.toProviderFormat();
    EXPECT_EQ(result["thinking"].toString(), "hmm...");
    EXPECT_EQ(result["content"].toString(), "answer");
}

TEST(OllamaMessage, CreateToolResultMessages)
{
    OllamaMessage msg;
    msg.applyEvent(toolCallLine("read", QJsonObject{}));
    msg.applyEvent(toolCallLine("write", QJsonObject{}));

    const QList<ToolUseContent> tools = msg.currentToolUseContent();
    QHash<QString, ToolResult> results;
    results[tools[0].id] = ToolResult::text("content1");
    results[tools[1].id] = ToolResult::text("content2");

    const QJsonArray messages = msg.createToolResultMessages(results);
    ASSERT_EQ(messages.size(), 2);

    for (const QJsonValue &val : messages) {
        const QJsonObject obj = val.toObject();
        EXPECT_EQ(obj["role"].toString(), "tool");
        EXPECT_FALSE(obj["content"].toString().isEmpty());
    }
}

TEST(OllamaMessage, DoneWithToolsRequiresToolExecution)
{
    OllamaMessage msg;
    msg.applyEvent(toolCallLine("tool", QJsonObject{}));
    msg.applyEvent(doneLine());

    EXPECT_EQ(msg.state(), MessageState::RequiresToolExecution);
}

TEST(OllamaMessage, StartNewContinuation)
{
    OllamaMessage msg;
    msg.applyEvent(contentLine("old"));
    msg.applyEvent(thinkingLine("thought"));
    msg.applyEvent(toolCallLine("tool", QJsonObject{}));
    msg.applyEvent(doneLine());

    msg.startNewContinuation();
    EXPECT_EQ(msg.state(), MessageState::Building);
    EXPECT_TRUE(msg.stopReason().isEmpty());
    EXPECT_TRUE(msg.currentBlocks().isEmpty());
    EXPECT_TRUE(msg.currentToolUseContent().isEmpty());
    EXPECT_TRUE(msg.currentThinkingContent().isEmpty());
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
