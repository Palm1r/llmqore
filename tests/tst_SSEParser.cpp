// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <LLMCore/SSEParser.hpp>

using namespace LLMCore;

TEST(SSEParser, InitialStateIsEmpty)
{
    SSEParser p;
    EXPECT_FALSE(p.hasIncompleteData());
}

TEST(SSEParser, SingleDefaultMessageEvent)
{
    SSEParser p;
    const auto events = p.append("data: hello\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, QStringLiteral("message"));
    EXPECT_EQ(events[0].data, QByteArray("hello"));
    EXPECT_TRUE(events[0].id.isEmpty());
}

TEST(SSEParser, ExplicitEventType)
{
    SSEParser p;
    const auto events = p.append("event: response.created\ndata: {\"id\":\"r1\"}\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, QStringLiteral("response.created"));
    EXPECT_EQ(events[0].data, QByteArray("{\"id\":\"r1\"}"));
}

TEST(SSEParser, MultiLineDataJoinedWithLF)
{
    SSEParser p;
    const auto events = p.append("data: line1\ndata: line2\ndata: line3\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, QByteArray("line1\nline2\nline3"));
}

TEST(SSEParser, MultipleEventsInOneChunk)
{
    SSEParser p;
    const auto events = p.append(
        "event: a\ndata: 1\n\n"
        "event: b\ndata: 2\n\n"
        "event: c\ndata: 3\n\n");
    ASSERT_EQ(events.size(), 3);
    EXPECT_EQ(events[0].type, "a");
    EXPECT_EQ(events[0].data, "1");
    EXPECT_EQ(events[1].type, "b");
    EXPECT_EQ(events[1].data, "2");
    EXPECT_EQ(events[2].type, "c");
    EXPECT_EQ(events[2].data, "3");
}

TEST(SSEParser, CrlfLineEndingsNormalized)
{
    SSEParser p;
    const auto events = p.append("event: x\r\ndata: y\r\n\r\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, "x");
    EXPECT_EQ(events[0].data, "y");
}

TEST(SSEParser, BareCrLineEndingsNormalized)
{
    SSEParser p;
    const auto events = p.append("data: z\r\r");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "z");
}

TEST(SSEParser, CommentLinesIgnored)
{
    SSEParser p;
    const auto events = p.append(": keep-alive\ndata: payload\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "payload");
}

TEST(SSEParser, EventOnlyBlockIsDiscarded)
{
    // Per spec, a block without a `data:` field must NOT be dispatched.
    SSEParser p;
    const auto events = p.append("event: noop\n\n");
    EXPECT_EQ(events.size(), 0);
}

TEST(SSEParser, RetryFieldIgnored)
{
    SSEParser p;
    const auto events = p.append("retry: 5000\ndata: hi\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "hi");
}

TEST(SSEParser, IdFieldCaptured)
{
    SSEParser p;
    const auto events = p.append("id: 42\ndata: hello\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].id, QByteArray("42"));
    EXPECT_EQ(events[0].data, "hello");
}

TEST(SSEParser, LeadingSpaceAfterColonStripped)
{
    // "data: foo" and "data:foo" should both yield "foo".
    SSEParser p;
    auto events = p.append("data:foo\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "foo");

    p.clear();
    events = p.append("data: foo\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "foo");
}

TEST(SSEParser, EventSplitAcrossChunks)
{
    SSEParser p;
    auto part1 = p.append("event: res");
    EXPECT_EQ(part1.size(), 0);

    auto part2 = p.append("ponse\ndata: ");
    EXPECT_EQ(part2.size(), 0);
    EXPECT_TRUE(p.hasIncompleteData());

    auto part3 = p.append("{\"ok\":true}\n\n");
    ASSERT_EQ(part3.size(), 1);
    EXPECT_EQ(part3[0].type, "response");
    EXPECT_EQ(part3[0].data, "{\"ok\":true}");
}

TEST(SSEParser, FlushForcesPendingEventAtEndOfStream)
{
    // Server closed without a trailing blank line.
    SSEParser p;
    auto in_progress = p.append("event: done\ndata: final");
    EXPECT_EQ(in_progress.size(), 0);
    EXPECT_TRUE(p.hasIncompleteData());

    const auto flushed = p.flush();
    ASSERT_EQ(flushed.size(), 1);
    EXPECT_EQ(flushed[0].type, "done");
    EXPECT_EQ(flushed[0].data, "final");

    EXPECT_FALSE(p.hasIncompleteData());
}

TEST(SSEParser, FlushOnEmptyParserReturnsNothing)
{
    SSEParser p;
    const auto flushed = p.flush();
    EXPECT_EQ(flushed.size(), 0);
}

TEST(SSEParser, ClearResets)
{
    SSEParser p;
    p.append("event: x\ndata: partial");
    EXPECT_TRUE(p.hasIncompleteData());
    p.clear();
    EXPECT_FALSE(p.hasIncompleteData());
    const auto events = p.append("data: fresh\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, "message");  // reset back to default
    EXPECT_EQ(events[0].data, "fresh");
}

TEST(SSEParser, BufferCeilingDropsGarbage)
{
    // Set a tiny ceiling and feed a huge chunk with no terminator.
    SSEParser p;
    p.setMaxBufferBytes(64);
    auto events = p.append(QByteArray(1024, 'x'));
    EXPECT_EQ(events.size(), 0);
    // After the drop, the parser should recover and parse the next event.
    events = p.append("data: ok\n\n");
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].data, "ok");
}

// ---- format() ----

TEST(SSEParser, FormatDefaultMessageOmitsEventField)
{
    const QByteArray out = SSEParser::format(QByteArray("hello"));
    EXPECT_EQ(out, QByteArray("data: hello\r\n\r\n"));
}

TEST(SSEParser, FormatWithExplicitMessageTypeOmitsEventField)
{
    const QByteArray out = SSEParser::format(QByteArray("hi"), QByteArray("message"));
    // "message" is the default — format() omits the event: line.
    EXPECT_EQ(out, QByteArray("data: hi\r\n\r\n"));
}

TEST(SSEParser, FormatWithCustomTypeEmitsEventField)
{
    const QByteArray out = SSEParser::format(QByteArray("{\"x\":1}"), QByteArray("response.created"));
    EXPECT_EQ(out, QByteArray("event: response.created\r\ndata: {\"x\":1}\r\n\r\n"));
}

TEST(SSEParser, FormatMultiLineDataSplitsAcrossDataLines)
{
    const QByteArray out = SSEParser::format(QByteArray("one\ntwo\nthree"));
    EXPECT_EQ(out, QByteArray("data: one\r\ndata: two\r\ndata: three\r\n\r\n"));
}

TEST(SSEParser, FormatWithIdEmitsIdField)
{
    const QByteArray out = SSEParser::format(
        QByteArray("payload"), QByteArray("alert"), QByteArray("evt-42"));
    EXPECT_EQ(out, QByteArray("event: alert\r\ndata: payload\r\nid: evt-42\r\n\r\n"));
}

TEST(SSEParser, FormatEmptyDataStillEmitsDataField)
{
    const QByteArray out = SSEParser::format(QByteArray());
    EXPECT_EQ(out, QByteArray("data:\r\n\r\n"));
}

TEST(SSEParser, FormatAppendRoundTrip)
{
    // Format an event, parse it back, expect the same fields (modulo
    // type normalisation to "message" when omitted on the wire).
    const QByteArray wire = SSEParser::format(
        QByteArray("line1\nline2"), QByteArray("custom"), QByteArray("42"));

    SSEParser p;
    const auto events = p.append(wire);
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, QStringLiteral("custom"));
    EXPECT_EQ(events[0].data, QByteArray("line1\nline2"));
    EXPECT_EQ(events[0].id, QByteArray("42"));
}

TEST(SSEParser, FormatEventStructOverloadMatches)
{
    SSEEvent ev;
    ev.type = QStringLiteral("ping");
    ev.data = QByteArray("payload");
    ev.id = QByteArray("7");
    const QByteArray out = SSEParser::format(ev);
    EXPECT_EQ(out, QByteArray("event: ping\r\ndata: payload\r\nid: 7\r\n\r\n"));
}
