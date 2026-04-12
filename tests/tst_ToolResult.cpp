// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <LLMCore/ToolResult.hpp>

using namespace LLMCore;

// ---------- ToolResult convenience factories ----------

TEST(ToolResultTest, TextFactoryProducesSingleTextBlock)
{
    const ToolResult r = ToolResult::text("hello world");
    EXPECT_FALSE(r.isError);
    ASSERT_EQ(r.content.size(), 1);
    EXPECT_EQ(r.content.first().type, ToolContent::Text);
    EXPECT_EQ(r.content.first().text, "hello world");
    EXPECT_EQ(r.asText(), "hello world");
}

TEST(ToolResultTest, ErrorFactorySetsIsErrorAndText)
{
    const ToolResult r = ToolResult::error("oops");
    EXPECT_TRUE(r.isError);
    ASSERT_EQ(r.content.size(), 1);
    EXPECT_EQ(r.content.first().type, ToolContent::Text);
    EXPECT_EQ(r.content.first().text, "oops");
}

TEST(ToolResultTest, EmptyFactoryHasNoContent)
{
    const ToolResult r = ToolResult::empty();
    EXPECT_FALSE(r.isError);
    EXPECT_TRUE(r.content.isEmpty());
    EXPECT_TRUE(r.isEmpty());
    EXPECT_TRUE(r.asText().isEmpty());
}

// ---------- asText flattening ----------

TEST(ToolResultTest, AsTextJoinsMultipleTextBlocks)
{
    ToolResult r;
    r.content.append(ToolContent::makeText("Hello"));
    r.content.append(ToolContent::makeText("World"));
    EXPECT_EQ(r.asText(), "Hello\nWorld");
}

TEST(ToolResultTest, AsTextSubstitutesImageBlock)
{
    ToolResult r;
    r.content.append(ToolContent::makeText("Before"));
    r.content.append(ToolContent::makeImage(QByteArray("\x01\x02", 2), "image/png"));
    r.content.append(ToolContent::makeText("After"));
    const QString text = r.asText();
    EXPECT_TRUE(text.contains("Before"));
    EXPECT_TRUE(text.contains("After"));
    EXPECT_TRUE(text.contains("[image: image/png]"));
}

TEST(ToolResultTest, AsTextSubstitutesAudioBlock)
{
    ToolResult r;
    r.content.append(ToolContent::makeAudio(QByteArray("AUDIO", 5), "audio/wav"));
    EXPECT_EQ(r.asText(), "[audio: audio/wav]");
}

TEST(ToolResultTest, AsTextInlinesEmbeddedResourceText)
{
    ToolResult r;
    r.content.append(ToolContent::makeResourceText(
        "mem://note", "line1\nline2", "text/plain"));
    EXPECT_EQ(r.asText(), "line1\nline2");
}

TEST(ToolResultTest, AsTextSubstitutesEmbeddedResourceBlob)
{
    ToolResult r;
    r.content.append(
        ToolContent::makeResourceBlob("mem://blob", QByteArray("\x00\x01", 2)));
    EXPECT_EQ(r.asText(), "[resource: mem://blob]");
}

TEST(ToolResultTest, AsTextSubstitutesResourceLink)
{
    ToolResult r;
    r.content.append(
        ToolContent::makeResourceLink("https://example.com/a.txt", "A", "An A"));
    EXPECT_EQ(r.asText(), "[resource link: https://example.com/a.txt]");
}

// ---------- JSON round trips ----------

TEST(ToolResultTest, TextContentRoundTrip)
{
    const ToolContent original = ToolContent::makeText("hi");
    const QJsonObject json = original.toJson();
    EXPECT_EQ(json.value("type").toString(), "text");
    EXPECT_EQ(json.value("text").toString(), "hi");

    const ToolContent back = ToolContent::fromJson(json);
    EXPECT_EQ(back.type, ToolContent::Text);
    EXPECT_EQ(back.text, "hi");
}

TEST(ToolResultTest, ImageContentRoundTripUsesBase64)
{
    const QByteArray bytes("\x01\x02\xff", 3);
    const ToolContent original = ToolContent::makeImage(bytes, "image/png");
    const QJsonObject json = original.toJson();
    EXPECT_EQ(json.value("type").toString(), "image");
    EXPECT_EQ(json.value("mimeType").toString(), "image/png");
    EXPECT_EQ(
        QByteArray::fromBase64(json.value("data").toString().toUtf8()), bytes);

    const ToolContent back = ToolContent::fromJson(json);
    EXPECT_EQ(back.type, ToolContent::Image);
    EXPECT_EQ(back.data, bytes);
    EXPECT_EQ(back.mimeType, "image/png");
}

TEST(ToolResultTest, AudioContentRoundTrip)
{
    const QByteArray bytes("audio-bytes", 11);
    const ToolContent original = ToolContent::makeAudio(bytes, "audio/mpeg");
    const QJsonObject json = original.toJson();
    EXPECT_EQ(json.value("type").toString(), "audio");

    const ToolContent back = ToolContent::fromJson(json);
    EXPECT_EQ(back.type, ToolContent::Audio);
    EXPECT_EQ(back.data, bytes);
    EXPECT_EQ(back.mimeType, "audio/mpeg");
}

TEST(ToolResultTest, EmbeddedResourceTextRoundTrip)
{
    const ToolContent original = ToolContent::makeResourceText(
        "mem://readme", "contents", "text/markdown");
    const QJsonObject json = original.toJson();
    EXPECT_EQ(json.value("type").toString(), "resource");
    const QJsonObject inner = json.value("resource").toObject();
    EXPECT_EQ(inner.value("uri").toString(), "mem://readme");
    EXPECT_EQ(inner.value("text").toString(), "contents");
    EXPECT_EQ(inner.value("mimeType").toString(), "text/markdown");

    const ToolContent back = ToolContent::fromJson(json);
    EXPECT_EQ(back.type, ToolContent::Resource);
    EXPECT_EQ(back.uri, "mem://readme");
    EXPECT_EQ(back.resourceText, "contents");
    EXPECT_EQ(back.mimeType, "text/markdown");
}

TEST(ToolResultTest, EmbeddedResourceBlobRoundTrip)
{
    const QByteArray bytes("\xFF\xD8\xFF", 3); // JPEG magic
    const ToolContent original = ToolContent::makeResourceBlob(
        "mem://thumb", bytes, "image/jpeg");
    const QJsonObject json = original.toJson();
    const QJsonObject inner = json.value("resource").toObject();
    EXPECT_TRUE(inner.contains("blob"));
    EXPECT_FALSE(inner.contains("text"));

    const ToolContent back = ToolContent::fromJson(json);
    EXPECT_EQ(back.type, ToolContent::Resource);
    EXPECT_EQ(back.resourceBlob, bytes);
}

TEST(ToolResultTest, ResourceLinkRoundTrip)
{
    const ToolContent original = ToolContent::makeResourceLink(
        "file:///tmp/log.txt", "Log", "Build log", "text/plain");
    const QJsonObject json = original.toJson();
    EXPECT_EQ(json.value("type").toString(), "resource_link");
    EXPECT_EQ(json.value("uri").toString(), "file:///tmp/log.txt");
    EXPECT_EQ(json.value("name").toString(), "Log");

    const ToolContent back = ToolContent::fromJson(json);
    EXPECT_EQ(back.type, ToolContent::ResourceLink);
    EXPECT_EQ(back.uri, "file:///tmp/log.txt");
    EXPECT_EQ(back.name, "Log");
    EXPECT_EQ(back.description, "Build log");
    EXPECT_EQ(back.mimeType, "text/plain");
}

TEST(ToolResultTest, UnknownContentTypeFallsBackToTextPlaceholder)
{
    const QJsonObject obj{{"type", "weird-future-type"}};
    const ToolContent back = ToolContent::fromJson(obj);
    EXPECT_EQ(back.type, ToolContent::Text);
    EXPECT_TRUE(back.text.contains("weird-future-type"));
}

// ---------- full ToolResult envelope round trip ----------

TEST(ToolResultTest, FullEnvelopeRoundTripPreservesEverything)
{
    ToolResult original;
    original.content.append(ToolContent::makeText("summary"));
    original.content.append(
        ToolContent::makeImage(QByteArray("png", 3), "image/png"));
    original.isError = false;
    original.structuredContent
        = QJsonObject{{"status", "ok"}, {"items", 3}};

    const QJsonObject json = original.toJson();
    EXPECT_EQ(json.value("content").toArray().size(), 2);
    EXPECT_FALSE(json.contains("isError"));
    EXPECT_EQ(json.value("structuredContent").toObject().value("status").toString(),
              "ok");

    const ToolResult back = ToolResult::fromJson(json);
    ASSERT_EQ(back.content.size(), 2);
    EXPECT_EQ(back.content.at(0).type, ToolContent::Text);
    EXPECT_EQ(back.content.at(0).text, "summary");
    EXPECT_EQ(back.content.at(1).type, ToolContent::Image);
    EXPECT_EQ(back.structuredContent.value("items").toInt(), 3);
    EXPECT_FALSE(back.isError);
}

TEST(ToolResultTest, IsErrorEnvelopeRoundTrip)
{
    const ToolResult original = ToolResult::error("something broke");
    const QJsonObject json = original.toJson();
    EXPECT_TRUE(json.value("isError").toBool());

    const ToolResult back = ToolResult::fromJson(json);
    EXPECT_TRUE(back.isError);
    EXPECT_EQ(back.asText(), "something broke");
}
