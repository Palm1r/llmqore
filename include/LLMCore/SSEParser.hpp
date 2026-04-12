// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore {

struct LLMCORE_EXPORT SSEEvent
{
    QString type;
    QByteArray data;
    QByteArray id;
};

class LLMCORE_EXPORT SSEParser
{
public:
    static constexpr qsizetype kDefaultMaxBufferBytes = 16 * 1024 * 1024;

    SSEParser() = default;

    [[nodiscard]] QList<SSEEvent> append(const QByteArray &chunk);

    [[nodiscard]] QList<SSEEvent> flush();

    void clear();

    [[nodiscard]] static QByteArray format(const SSEEvent &event);
    [[nodiscard]] static QByteArray format(
        const QByteArray &data,
        const QByteArray &type = {},
        const QByteArray &id = {});

    void setMaxBufferBytes(qsizetype bytes) noexcept { m_maxBufferBytes = bytes; }
    [[nodiscard]] qsizetype maxBufferBytes() const noexcept { return m_maxBufferBytes; }

    [[nodiscard]] bool hasIncompleteData() const noexcept { return !m_buffer.isEmpty(); }

private:
    QByteArray m_buffer;
    SSEEvent m_current;
    qsizetype m_maxBufferBytes = kDefaultMaxBufferBytes;
};

} // namespace LLMCore
