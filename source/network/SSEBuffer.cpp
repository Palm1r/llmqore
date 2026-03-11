// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/SSEBuffer.hpp>

namespace LLMCore {

QStringList SSEBuffer::processData(const QByteArray &data)
{
    m_buffer += QString::fromUtf8(data);

    QStringList lines = m_buffer.split('\n');
    m_buffer = lines.takeLast();

    // Do NOT remove empty lines here — they are SSE event delimiters.
    // Each client is responsible for filtering lines it does not need.

    return lines;
}

void SSEBuffer::clear()
{
    m_buffer.clear();
}

QString SSEBuffer::currentBuffer() const
{
    return m_buffer;
}

bool SSEBuffer::hasIncompleteData() const
{
    return !m_buffer.isEmpty();
}

} // namespace LLMCore
