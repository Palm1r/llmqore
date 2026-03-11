// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QJsonObject>

#include <LLMCore/ContentBlocks.hpp>

using namespace LLMCore;

TEST(TextContent, Type)
{
    TextContent tc("hello");
    EXPECT_EQ(tc.type(), "text");
}

TEST(TextContent, Text)
{
    TextContent tc("hello");
    EXPECT_EQ(tc.text(), "hello");
}

TEST(TextContent, AppendText)
{
    TextContent tc("hello");
    tc.appendText(" world");
    EXPECT_EQ(tc.text(), "hello world");
}

TEST(TextContent, SetText)
{
    TextContent tc("hello");
    tc.setText("replaced");
    EXPECT_EQ(tc.text(), "replaced");
}

TEST(TextContent, Empty)
{
    TextContent tc;
    EXPECT_EQ(tc.text(), QString());
}

TEST(ImageContent, Base64)
{
    ImageContent ic("base64data", "image/png", ImageContent::ImageSourceType::Base64);
    EXPECT_EQ(ic.type(), "image");
    EXPECT_EQ(ic.data(), "base64data");
    EXPECT_EQ(ic.mediaType(), "image/png");
    EXPECT_EQ(ic.sourceType(), ImageContent::ImageSourceType::Base64);
}

TEST(ImageContent, Url)
{
    ImageContent ic("https://example.com/img.png", "", ImageContent::ImageSourceType::Url);
    EXPECT_EQ(ic.type(), "image");
    EXPECT_EQ(ic.data(), "https://example.com/img.png");
    EXPECT_EQ(ic.sourceType(), ImageContent::ImageSourceType::Url);
}

TEST(ImageContent, DefaultSourceTypeIsBase64)
{
    ImageContent ic("data123", "image/png");
    EXPECT_EQ(ic.sourceType(), ImageContent::ImageSourceType::Base64);
}

TEST(ImageContent, LargeBase64Data)
{
    QString largeData(10000, 'A');
    ImageContent ic(largeData, "image/png", ImageContent::ImageSourceType::Base64);
    EXPECT_EQ(ic.data(), largeData);
}

TEST(ImageContent, DifferentMediaTypes)
{
    ImageContent jpeg("data", "image/jpeg");
    EXPECT_EQ(jpeg.mediaType(), "image/jpeg");

    ImageContent webp("data", "image/webp");
    EXPECT_EQ(webp.mediaType(), "image/webp");

    ImageContent gif("data", "image/gif");
    EXPECT_EQ(gif.mediaType(), "image/gif");
}

TEST(ToolUseContent, Basic)
{
    QJsonObject input{{"key", "value"}};
    ToolUseContent tuc("tool-123", "read_file", input);
    EXPECT_EQ(tuc.type(), "tool_use");
    EXPECT_EQ(tuc.id(), "tool-123");
    EXPECT_EQ(tuc.name(), "read_file");
    EXPECT_EQ(tuc.input(), input);
}

TEST(ToolUseContent, SetInput)
{
    ToolUseContent tuc("id", "name");
    EXPECT_TRUE(tuc.input().isEmpty());
    QJsonObject newInput{{"file", "test.cpp"}};
    tuc.setInput(newInput);
    EXPECT_EQ(tuc.input(), newInput);
}

TEST(ToolResultContent, Basic)
{
    ToolResultContent trc("tool-1", "file contents here");
    EXPECT_EQ(trc.type(), "tool_result");
    EXPECT_EQ(trc.toolUseId(), "tool-1");
    EXPECT_EQ(trc.result(), "file contents here");
}

TEST(ThinkingContent, Basic)
{
    ThinkingContent tc("I think...", "sig123");
    EXPECT_EQ(tc.type(), "thinking");
    EXPECT_EQ(tc.thinking(), "I think...");
    EXPECT_EQ(tc.signature(), "sig123");
}

TEST(ThinkingContent, Append)
{
    ThinkingContent tc("start");
    tc.appendThinking(" more");
    EXPECT_EQ(tc.thinking(), "start more");
}

TEST(ThinkingContent, SetThinking)
{
    ThinkingContent tc("original");
    tc.setThinking("replaced");
    EXPECT_EQ(tc.thinking(), "replaced");
}

TEST(ThinkingContent, SetSignature)
{
    ThinkingContent tc("thought");
    EXPECT_TRUE(tc.signature().isEmpty());
    tc.setSignature("sig");
    EXPECT_EQ(tc.signature(), "sig");
}

TEST(RedactedThinkingContent, Basic)
{
    RedactedThinkingContent rtc("sig456");
    EXPECT_EQ(rtc.type(), "redacted_thinking");
    EXPECT_EQ(rtc.signature(), "sig456");
}

TEST(RedactedThinkingContent, SetSignature)
{
    RedactedThinkingContent rtc;
    EXPECT_TRUE(rtc.signature().isEmpty());
    rtc.setSignature("sig");
    EXPECT_EQ(rtc.signature(), "sig");
}

TEST(AddContentBlock, AddsToList)
{
    QList<ContentBlock *> blocks;
    auto *tc = addContentBlock<TextContent>(blocks, "hello");
    EXPECT_EQ(blocks.size(), 1);
    EXPECT_EQ(tc->text(), "hello");
    qDeleteAll(blocks);
}
