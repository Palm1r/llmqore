// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/SSEParser.hpp>

#include <LLMCore/Log.hpp>

namespace LLMCore {

QList<SSEEvent> SSEParser::append(const QByteArray &chunk)
{
    QList<SSEEvent> events;
    m_buffer.append(chunk);

    m_buffer.replace("\r\n", "\n");
    m_buffer.replace('\r', '\n');

    while (true) {
        const int idx = m_buffer.indexOf('\n');
        if (idx < 0) {
            if (m_maxBufferBytes > 0 && m_buffer.size() > m_maxBufferBytes) {
                qCWarning(llmNetworkLog).noquote()
                    << QString("SSE buffer exceeded %1 bytes with no line "
                               "terminator; dropping")
                           .arg(m_maxBufferBytes);
                m_buffer.clear();
                m_current = SSEEvent{};
            }
            break;
        }

        const QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);

        if (line.isEmpty()) {
            if (!m_current.data.isEmpty()) {
                if (m_current.type.isEmpty())
                    m_current.type = QStringLiteral("message");
                events.append(m_current);
            }
            m_current = SSEEvent{};
            continue;
        }

        if (line.startsWith(':'))
            continue;

        const int colon = line.indexOf(':');
        QByteArray field;
        QByteArray value;
        if (colon >= 0) {
            field = line.left(colon);
            value = line.mid(colon + 1);
            if (value.startsWith(' '))
                value.remove(0, 1);
        } else {
            field = line;
        }

        if (field == "event") {
            m_current.type = QString::fromUtf8(value);
        } else if (field == "data") {
            if (!m_current.data.isEmpty())
                m_current.data.append('\n');
            m_current.data.append(value);
        } else if (field == "id") {
            m_current.id = value;
        }
    }

    return events;
}

QList<SSEEvent> SSEParser::flush()
{
    QList<SSEEvent> events;
    if (!m_buffer.isEmpty())
        events = append(QByteArray("\n\n"));

    if (!m_current.data.isEmpty()) {
        if (m_current.type.isEmpty())
            m_current.type = QStringLiteral("message");
        events.append(m_current);
    }
    clear();
    return events;
}

void SSEParser::clear()
{
    m_buffer.clear();
    m_current = SSEEvent{};
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

} // namespace LLMCore
