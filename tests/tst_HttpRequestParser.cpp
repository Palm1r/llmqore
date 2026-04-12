// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <LLMCore/HttpRequestParser.hpp>

using namespace LLMCore;

namespace {

HttpRequestParser::Status parseAll(HttpRequestParser &p, QList<HttpRequest> &out)
{
    while (true) {
        const auto status = p.parseNext();
        if (status == HttpRequestParser::Status::Complete) {
            out.append(p.takeRequest());
            continue;
        }
        return status;
    }
}

} // namespace

TEST(HttpRequest, CaseInsensitiveHeaderLookup)
{
    HttpRequest req;
    req.headers.insert("content-length", "42");
    req.headers.insert("x-custom", "v");

    EXPECT_EQ(req.header(QByteArrayView("Content-Length")), "42");
    EXPECT_EQ(req.header(QByteArrayView("content-length")), "42");
    EXPECT_EQ(req.header(QByteArrayView("CONTENT-LENGTH")), "42");
    EXPECT_EQ(req.header(QByteArrayView("X-Custom")), "v");
    EXPECT_EQ(req.header(QByteArrayView("missing")), QByteArray());
}

TEST(HttpRequestParser, InitialStateNeedsData)
{
    HttpRequestParser p;
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::NeedMoreData);
    EXPECT_FALSE(p.hasPendingData());
}

TEST(HttpRequestParser, SimpleGetRequest)
{
    HttpRequestParser p;
    p.feed("GET /mcp HTTP/1.1\r\nHost: localhost\r\n\r\n");

    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::Complete);
    const auto req = p.takeRequest();

    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.target, "/mcp");
    EXPECT_EQ(req.httpVersion, "HTTP/1.1");
    EXPECT_EQ(req.header(QByteArrayView("host")), "localhost");
    EXPECT_TRUE(req.body.isEmpty());
}

TEST(HttpRequestParser, PostWithJsonBody)
{
    HttpRequestParser p;
    p.feed(
        "POST /mcp HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"hello\":1}\r\n");  // 13 bytes including the trailing CRLF

    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::Complete);
    const auto req = p.takeRequest();

    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.target, "/mcp");
    EXPECT_EQ(req.header(QByteArrayView("Content-Type")), "application/json");
    EXPECT_EQ(req.body.size(), 13);
    EXPECT_TRUE(req.body.startsWith("{\"hello\":1}"));
}

TEST(HttpRequestParser, HeadersSplitAcrossChunks)
{
    HttpRequestParser p;
    p.feed("POST /mcp HTTP/1.1\r\nHos");
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::NeedMoreData);

    p.feed("t: localhost\r\nContent-Length: 2\r\n\r\nab");
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::Complete);
    const auto req = p.takeRequest();
    EXPECT_EQ(req.body, "ab");
    EXPECT_EQ(req.header(QByteArrayView("host")), "localhost");
}

TEST(HttpRequestParser, BodySplitAcrossChunks)
{
    HttpRequestParser p;
    p.feed("POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nhe");
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::NeedMoreData);

    p.feed("llo");
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::Complete);
    EXPECT_EQ(p.takeRequest().body, "hello");
}

TEST(HttpRequestParser, PipelinedRequestsInSingleFeed)
{
    HttpRequestParser p;
    // Two full POSTs back-to-back in one buffer.
    p.feed(
        "POST / HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc"
        "POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\nwxyz");

    QList<HttpRequest> out;
    const auto final = parseAll(p, out);
    EXPECT_EQ(final, HttpRequestParser::Status::NeedMoreData);
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out[0].body, "abc");
    EXPECT_EQ(out[1].body, "wxyz");
}

TEST(HttpRequestParser, EmptyBodyWithContentLengthZero)
{
    HttpRequestParser p;
    p.feed("POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::Complete);
    const auto req = p.takeRequest();
    EXPECT_TRUE(req.body.isEmpty());
}

TEST(HttpRequestParser, NoContentLengthTreatedAsEmptyBody)
{
    HttpRequestParser p;
    p.feed("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::Complete);
    const auto req = p.takeRequest();
    EXPECT_EQ(req.method, "GET");
    EXPECT_TRUE(req.body.isEmpty());
}

TEST(HttpRequestParser, HeaderOverflow)
{
    HttpRequestParser p;
    p.setMaxHeaderBytes(128);
    // Feed a header block longer than the cap with NO terminator.
    p.feed(QByteArray(512, 'x'));

    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::HeaderOverflow);
}

TEST(HttpRequestParser, BodyTooLarge)
{
    HttpRequestParser p;
    p.setMaxBodyBytes(64);
    p.feed("POST / HTTP/1.1\r\nContent-Length: 1024\r\n\r\n");

    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::BodyTooLarge);
}

TEST(HttpRequestParser, InvalidContentLength)
{
    HttpRequestParser p;
    p.feed("POST / HTTP/1.1\r\nContent-Length: not-a-number\r\n\r\n");

    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::InvalidContentLength);
}

TEST(HttpRequestParser, InvalidRequestLine)
{
    HttpRequestParser p;
    p.feed("GARBAGE\r\n\r\n");
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::InvalidRequestLine);
}

TEST(HttpRequestParser, ResetClearsAllState)
{
    HttpRequestParser p;
    p.feed("POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\nhe");
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::NeedMoreData);
    EXPECT_TRUE(p.hasPendingData());

    p.reset();
    EXPECT_FALSE(p.hasPendingData());
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::NeedMoreData);

    p.feed("GET / HTTP/1.1\r\n\r\n");
    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::Complete);
    EXPECT_EQ(p.takeRequest().method, "GET");
}

TEST(HttpRequestParser, HeaderKeysStoredLowercased)
{
    HttpRequestParser p;
    p.feed("POST / HTTP/1.1\r\nX-Custom-Header: v1\r\nContent-Length: 0\r\n\r\n");

    EXPECT_EQ(p.parseNext(), HttpRequestParser::Status::Complete);
    const auto req = p.takeRequest();
    // Stored lowercased, accessible via any case.
    EXPECT_TRUE(req.headers.contains("x-custom-header"));
    EXPECT_FALSE(req.headers.contains("X-Custom-Header"));
    EXPECT_EQ(req.header(QByteArrayView("X-Custom-Header")), "v1");
}
