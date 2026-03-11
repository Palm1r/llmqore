// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonObject>

#include <LLMCore/SSEUtils.hpp>

using namespace LLMCore;

TEST(SSEUtils, ParseValidJsonEvent)
{
    QJsonObject result = SSEUtils::parseEventLine(R"(data: {"type":"message_start"})");
    EXPECT_FALSE(result.isEmpty());
    EXPECT_EQ(result["type"].toString(), "message_start");
}

TEST(SSEUtils, ParseNestedJsonEvent)
{
    QJsonObject result = SSEUtils::parseEventLine(R"(data: {"delta":{"text":"hello"},"index":0})");
    EXPECT_FALSE(result.isEmpty());
    EXPECT_EQ(result["index"].toInt(), 0);
    EXPECT_EQ(result["delta"].toObject()["text"].toString(), "hello");
}

TEST(SSEUtils, ParseDoneEvent)
{
    QJsonObject result = SSEUtils::parseEventLine("data: [DONE]");
    EXPECT_TRUE(result.isEmpty());
}

TEST(SSEUtils, ParseNonDataLine)
{
    QJsonObject result = SSEUtils::parseEventLine("event: message_start");
    EXPECT_TRUE(result.isEmpty());
}

TEST(SSEUtils, ParseEmptyLine)
{
    QJsonObject result = SSEUtils::parseEventLine("");
    EXPECT_TRUE(result.isEmpty());
}

TEST(SSEUtils, ParseInvalidJson)
{
    QJsonObject result = SSEUtils::parseEventLine("data: {invalid json}");
    EXPECT_TRUE(result.isEmpty());
}

TEST(SSEUtils, ParseEmptyJsonObject)
{
    QJsonObject result = SSEUtils::parseEventLine("data: {}");
    EXPECT_TRUE(result.isEmpty());
}

TEST(SSEUtils, ParseDataWithSpaces)
{
    QJsonObject result = SSEUtils::parseEventLine(R"(data: {"key": "value with spaces"})");
    EXPECT_EQ(result["key"].toString(), "value with spaces");
}
