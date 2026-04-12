// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <LLMCore/LineBuffer.hpp>

using namespace LLMCore;

TEST(LineBuffer, InitialState)
{
    LineBuffer buf;
    EXPECT_FALSE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), QString());
}

TEST(LineBuffer, SingleCompleteLine)
{
    LineBuffer buf;
    QStringList lines = buf.processData("{\"a\":1}\n");
    EXPECT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "{\"a\":1}");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(LineBuffer, MultipleCompleteLines)
{
    LineBuffer buf;
    QStringList lines = buf.processData("{\"a\":1}\n{\"b\":2}\n");
    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "{\"a\":1}");
    EXPECT_EQ(lines[1], "{\"b\":2}");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(LineBuffer, IncompleteDataHeldOver)
{
    LineBuffer buf;
    QStringList lines = buf.processData("{\"a\":");
    EXPECT_EQ(lines.size(), 0);
    EXPECT_TRUE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), "{\"a\":");
}

TEST(LineBuffer, SplitAcrossChunks)
{
    LineBuffer buf;

    QStringList lines1 = buf.processData("{\"key\":");
    EXPECT_EQ(lines1.size(), 0);

    QStringList lines2 = buf.processData("\"value\"}\n");
    EXPECT_EQ(lines2.size(), 1);
    EXPECT_EQ(lines2[0], "{\"key\":\"value\"}");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(LineBuffer, MixedCompleteAndIncomplete)
{
    LineBuffer buf;
    QStringList lines = buf.processData("a\nb\npartial");
    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "a");
    EXPECT_EQ(lines[1], "b");
    EXPECT_TRUE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), "partial");
}

TEST(LineBuffer, EmptyLinesPreserved)
{
    LineBuffer buf;
    QStringList lines = buf.processData("one\n\n\ntwo\n");
    EXPECT_EQ(lines.size(), 4);
    EXPECT_EQ(lines[0], "one");
    EXPECT_EQ(lines[1], "");
    EXPECT_EQ(lines[2], "");
    EXPECT_EQ(lines[3], "two");
}

TEST(LineBuffer, Clear)
{
    LineBuffer buf;
    buf.processData("partial");
    EXPECT_TRUE(buf.hasIncompleteData());

    buf.clear();
    EXPECT_FALSE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), QString());
}

TEST(LineBuffer, EmptyInput)
{
    LineBuffer buf;
    QStringList lines = buf.processData(QByteArray());
    EXPECT_EQ(lines.size(), 0);
}

TEST(LineBuffer, MultipleChunksAccumulate)
{
    LineBuffer buf;
    buf.processData("{\"");
    buf.processData("key\":");
    QStringList lines = buf.processData("\"val\"}\n");
    EXPECT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "{\"key\":\"val\"}");
}
