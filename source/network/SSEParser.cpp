// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/SSEParser.hpp>

#include <algorithm>
#include <limits>

#include <LLMQore/Log.hpp>

namespace LLMQore {

QList<SSEEvent> SSEParser::append(const QByteArray &chunk)
{
    QList<SSEEvent> events;
    const qsizetype scanFrom = m_buffer.size();
    if (m_skipLineFeed && !chunk.isEmpty()) {
        m_skipLineFeed = false;
        m_buffer.append(chunk.at(0) == '\n' ? chunk.mid(1) : chunk);
    } else {
        m_buffer.append(chunk);
    }

    qsizetype lineStart = 0;
    for (qsizetype i = scanFrom; i < m_buffer.size(); ++i) {
        const char c = m_buffer.at(i);
        if (c != '\n' && c != '\r')
            continue;
        processLine(m_buffer.mid(lineStart, i - lineStart), events);
        if (c == '\r') {
            if (i + 1 == m_buffer.size())
                m_skipLineFeed = true;
            else if (m_buffer.at(i + 1) == '\n')
                ++i;
        }
        lineStart = i + 1;
    }
    m_buffer.remove(0, lineStart);

    if (m_maxBufferBytes > 0 && m_buffer.size() > m_maxBufferBytes) {
        qCWarning(llmNetworkLog).noquote()
            << QString("SSE buffer exceeded %1 bytes with no line "
                       "terminator; dropping")
                   .arg(m_maxBufferBytes);
        m_buffer.clear();
        m_current = SSEEvent{};
    }

    return events;
}

void SSEParser::processLine(const QByteArray &line, QList<SSEEvent> &events)
{
    if (line.isEmpty()) {
        dispatch(events);
        return;
    }

    if (line.startsWith(':'))
        return;

    const qsizetype colon = line.indexOf(':');
    QByteArray field = line;
    QByteArray value;
    if (colon >= 0) {
        field = line.left(colon);
        value = line.mid(colon + 1);
        if (value.startsWith(' '))
            value.remove(0, 1);
    }

    if (field == "event") {
        m_current.type = QString::fromUtf8(value);
    } else if (field == "data") {
        if (m_droppingEvent)
            return;
        if (m_maxBufferBytes > 0 && m_current.data.size() + value.size() > m_maxBufferBytes) {
            qCWarning(llmNetworkLog).noquote()
                << QString("SSE event exceeded %1 bytes of data; dropping it")
                       .arg(m_maxBufferBytes);
            m_current.data.clear();
            m_droppingEvent = true;
            return;
        }
        if (!m_current.data.isEmpty())
            m_current.data.append('\n');
        m_current.data.append(value);
    } else if (field == "id") {
        if (!value.contains('\0'))
            m_idBuffer = value;
    } else if (field == "retry") {
        const bool digitsOnly = !value.isEmpty()
                                && std::all_of(value.cbegin(), value.cend(), [](char c) {
                                       return c >= '0' && c <= '9';
                                   });
        if (digitsOnly) {
            bool fits = false;
            const int milliseconds = value.toInt(&fits);
            m_retryMs = fits ? milliseconds : std::numeric_limits<int>::max();
        }
    }
}

void SSEParser::dispatch(QList<SSEEvent> &events)
{
    m_lastEventId = m_idBuffer;
    if (!m_droppingEvent && !m_current.data.isEmpty()) {
        if (m_current.type.isEmpty())
            m_current.type = QStringLiteral("message");
        m_current.id = m_lastEventId;
        events.append(m_current);
    }
    m_current = SSEEvent{};
    m_droppingEvent = false;
}

QList<SSEEvent> SSEParser::flush()
{
    m_skipLineFeed = false;
    QList<SSEEvent> events;
    if (!m_buffer.isEmpty())
        events = append(QByteArray("\n"));
    dispatch(events);
    clear();
    return events;
}

void SSEParser::clear()
{
    m_buffer.clear();
    m_current = SSEEvent{};
    m_idBuffer.clear();
    m_lastEventId.clear();
    m_retryMs.reset();
    m_skipLineFeed = false;
    m_droppingEvent = false;
}

QByteArray SSEParser::format(const SSEEvent &event)
{
    return format(event.data, event.type.toUtf8(), event.id);
}

QByteArray SSEParser::format(
    const QByteArray &data, const QByteArray &type, const QByteArray &id)
{
    QByteArray out;
    out.reserve(data.size() + type.size() + id.size() + 32);

    if (!type.isEmpty() && type != "message") {
        out.append("event: ");
        out.append(type);
        out.append("\r\n");
    }

    if (data.isEmpty()) {
        out.append("data:\r\n");
    } else {
        qsizetype start = 0;
        while (start <= data.size()) {
            const qsizetype nl = data.indexOf('\n', start);
            const qsizetype end = (nl < 0) ? data.size() : nl;
            out.append("data: ");
            out.append(data.mid(start, end - start));
            out.append("\r\n");
            if (nl < 0)
                break;
            start = nl + 1;
        }
    }

    if (!id.isEmpty()) {
        out.append("id: ");
        out.append(id);
        out.append("\r\n");
    }

    out.append("\r\n");
    return out;
}

} // namespace LLMQore
