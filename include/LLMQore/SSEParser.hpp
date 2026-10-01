// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QByteArray>
#include <QList>
#include <QString>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/SSEEvent.hpp>

namespace LLMQore {

class LLMQORE_EXPORT SSEParser
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

    void setLastEventId(const QByteArray &id)
    {
        m_idBuffer = id;
        m_lastEventId = id;
    }
    [[nodiscard]] QByteArray lastEventId() const { return m_lastEventId; }
    [[nodiscard]] std::optional<int> retryMs() const noexcept { return m_retryMs; }

    [[nodiscard]] bool hasIncompleteData() const noexcept { return !m_buffer.isEmpty(); }

private:
    void processLine(const QByteArray &line, QList<SSEEvent> &events);
    void dispatch(QList<SSEEvent> &events);

    QByteArray m_buffer;
    SSEEvent m_current;
    QByteArray m_idBuffer;
    QByteArray m_lastEventId;
    std::optional<int> m_retryMs;
    qsizetype m_maxBufferBytes = kDefaultMaxBufferBytes;
    bool m_skipLineFeed = false;
    bool m_droppingEvent = false;
};

} // namespace LLMQore
