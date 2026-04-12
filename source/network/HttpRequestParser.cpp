// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/HttpRequestParser.hpp>

#include <utility>

namespace LLMCore {

namespace {

QByteArray toLowerAscii(const QByteArray &in)
{
    QByteArray out(in);
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z')
            c = char(c + ('a' - 'A'));
    }
    return out;
}

} // namespace

QByteArray HttpRequest::header(QByteArrayView name) const
{
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        if (QByteArrayView(it.key()).compare(name, Qt::CaseInsensitive) == 0)
            return it.value();
    }
    return {};
}

void HttpRequestParser::feed(const QByteArray &bytes)
{
    m_buffer.append(bytes);
}

HttpRequestParser::Status HttpRequestParser::parseNext()
{
    if (m_phase == Phase::Complete)
        return Status::Complete;

    if (m_phase == Phase::ReadingHeaders) {
        const int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (m_buffer.size() > m_maxHeaderBytes)
                return Status::HeaderOverflow;
            return Status::NeedMoreData;
        }

        parseHeaderBlock(headerEnd);

        if (m_current.method.isEmpty() || m_current.target.isEmpty()
            || m_current.httpVersion.isEmpty()) {
            return Status::InvalidRequestLine;
        }

        const QByteArray cl = m_current.header(QByteArrayView("content-length"));
        if (cl.isEmpty()) {
            m_contentLength = 0;
        } else {
            bool ok = true;
            m_contentLength = cl.toLongLong(&ok);
            if (!ok || m_contentLength < 0)
                return Status::InvalidContentLength;
            if (m_contentLength > m_maxBodyBytes)
                return Status::BodyTooLarge;
        }
        m_phase = Phase::ReadingBody;
    }

    if (static_cast<qint64>(m_buffer.size()) < m_contentLength)
        return Status::NeedMoreData;

    m_current.body = m_buffer.left(static_cast<int>(m_contentLength));
    m_buffer.remove(0, static_cast<int>(m_contentLength));
    m_phase = Phase::Complete;
    return Status::Complete;
}

HttpRequest HttpRequestParser::takeRequest()
{
    HttpRequest out = std::move(m_current);
    m_current = HttpRequest{};
    m_contentLength = 0;
    m_phase = Phase::ReadingHeaders;
    return out;
}

void HttpRequestParser::reset()
{
    m_buffer.clear();
    m_current = HttpRequest{};
    m_contentLength = 0;
    m_phase = Phase::ReadingHeaders;
}

void HttpRequestParser::parseHeaderBlock(int headerEnd)
{
    const QByteArray headerBlock = m_buffer.left(headerEnd);
    m_buffer.remove(0, headerEnd + 4);

    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty())
        return;

    const QByteArray requestLine = lines.first().trimmed();
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() >= 3) {
        m_current.method = parts[0];
        m_current.target = parts[1];
        m_current.httpVersion = parts[2];
    }

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        if (line.isEmpty())
            continue;
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray key = toLowerAscii(line.left(colon).trimmed());
        const QByteArray value = line.mid(colon + 1).trimmed();
        m_current.headers.insert(key, value);
    }
}

} // namespace LLMCore
