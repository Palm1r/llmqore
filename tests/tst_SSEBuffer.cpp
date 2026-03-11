// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <LLMCore/SSEBuffer.hpp>

using namespace LLMCore;

TEST(SSEBuffer, InitialState)
{
    SSEBuffer buf;
    EXPECT_FALSE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), QString());
}

TEST(SSEBuffer, SingleCompleteLine)
{
    SSEBuffer buf;
    QStringList lines = buf.processData("data: hello\n");
    EXPECT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "data: hello");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(SSEBuffer, MultipleCompleteLines)
{
    SSEBuffer buf;
    QStringList lines = buf.processData("data: line1\ndata: line2\n");
    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "data: line1");
    EXPECT_EQ(lines[1], "data: line2");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(SSEBuffer, IncompleteData)
{
    SSEBuffer buf;
    QStringList lines = buf.processData("data: incomp");
    EXPECT_EQ(lines.size(), 0);
    EXPECT_TRUE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), "data: incomp");
}

TEST(SSEBuffer, SplitAcrossChunks)
{
    SSEBuffer buf;

    QStringList lines1 = buf.processData("data: hel");
    EXPECT_EQ(lines1.size(), 0);
    EXPECT_TRUE(buf.hasIncompleteData());

    QStringList lines2 = buf.processData("lo world\n");
    EXPECT_EQ(lines2.size(), 1);
    EXPECT_EQ(lines2[0], "data: hello world");
    EXPECT_FALSE(buf.hasIncompleteData());
}

TEST(SSEBuffer, MixedCompleteAndIncomplete)
{
    SSEBuffer buf;
    QStringList lines = buf.processData("data: first\ndata: second\ndata: partial");
    EXPECT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "data: first");
    EXPECT_EQ(lines[1], "data: second");
    EXPECT_TRUE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), "data: partial");
}

TEST(SSEBuffer, EmptyLinesPreservedAsDelimiters)
{
    SSEBuffer buf;
    QStringList lines = buf.processData("data: hello\n\n\ndata: world\n");
    EXPECT_EQ(lines.size(), 4);
    EXPECT_EQ(lines[0], "data: hello");
    EXPECT_EQ(lines[1], "");
    EXPECT_EQ(lines[2], "");
    EXPECT_EQ(lines[3], "data: world");
}

TEST(SSEBuffer, Clear)
{
    SSEBuffer buf;
    buf.processData("data: partial");
    EXPECT_TRUE(buf.hasIncompleteData());

    buf.clear();
    EXPECT_FALSE(buf.hasIncompleteData());
    EXPECT_EQ(buf.currentBuffer(), QString());
}

TEST(SSEBuffer, EmptyInput)
{
    SSEBuffer buf;
    QStringList lines = buf.processData(QByteArray());
    EXPECT_EQ(lines.size(), 0);
}

TEST(SSEBuffer, MultipleChunksAccumulate)
{
    SSEBuffer buf;
    buf.processData("da");
    buf.processData("ta: ");
    QStringList lines = buf.processData("test\n");
    EXPECT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "data: test");
}
