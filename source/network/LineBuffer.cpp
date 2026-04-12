// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/LineBuffer.hpp>

namespace LLMCore {

QStringList LineBuffer::processData(const QByteArray &data)
{
    m_buffer += QString::fromUtf8(data);

    QStringList lines = m_buffer.split('\n');
    m_buffer = lines.takeLast();

    return lines;
}

void LineBuffer::clear()
{
    m_buffer.clear();
}

QString LineBuffer::currentBuffer() const
{
    return m_buffer;
}

bool LineBuffer::hasIncompleteData() const
{
    return !m_buffer.isEmpty();
}

} // namespace LLMCore
