// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QByteArrayList>

namespace LLMCore::Mcp {

class McpLineFramer
{
public:
    QByteArrayList append(const QByteArray &data)
    {
        QByteArrayList lines;
        m_buffer.append(data);

        int start = 0;
        while (true) {
            const int nl = m_buffer.indexOf('\n', start);
            if (nl < 0)
                break;

            int end = nl;
            if (end > start && m_buffer.at(end - 1) == '\r')
                --end;

            QByteArray line = m_buffer.mid(start, end - start);
            if (!line.isEmpty())
                lines.append(line);

            start = nl + 1;
        }

        if (start > 0)
            m_buffer.remove(0, start);

        return lines;
    }

    void clear() { m_buffer.clear(); }
    bool hasIncompleteData() const { return !m_buffer.isEmpty(); }

private:
    QByteArray m_buffer;
};

} // namespace LLMCore::Mcp
