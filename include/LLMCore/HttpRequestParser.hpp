// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore {

struct LLMCORE_EXPORT HttpRequest
{
    QByteArray method;
    QByteArray target;
    QByteArray httpVersion;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;

    [[nodiscard]] QByteArray header(QByteArrayView name) const;
};

class LLMCORE_EXPORT HttpRequestParser
{
public:
    enum class Status {
        NeedMoreData,
        Complete,
        HeaderOverflow,
        InvalidRequestLine,
        InvalidContentLength,
        BodyTooLarge,
    };

    static constexpr qint64 kDefaultMaxHeaderBytes = 32 * 1024;
    static constexpr qint64 kDefaultMaxBodyBytes = 8 * 1024 * 1024;

    HttpRequestParser() = default;

    void feed(const QByteArray &bytes);

    [[nodiscard]] Status parseNext();

    [[nodiscard]] HttpRequest takeRequest();

    void reset();

    void setMaxHeaderBytes(qint64 bytes) noexcept { m_maxHeaderBytes = bytes; }
    void setMaxBodyBytes(qint64 bytes) noexcept { m_maxBodyBytes = bytes; }

    [[nodiscard]] qint64 maxHeaderBytes() const noexcept { return m_maxHeaderBytes; }
    [[nodiscard]] qint64 maxBodyBytes() const noexcept { return m_maxBodyBytes; }

    [[nodiscard]] bool hasPendingData() const noexcept { return !m_buffer.isEmpty(); }

private:
    enum class Phase { ReadingHeaders, ReadingBody, Complete };

    void parseHeaderBlock(int headerEnd);

    QByteArray m_buffer;
    HttpRequest m_current;
    Phase m_phase = Phase::ReadingHeaders;
    qint64 m_contentLength = 0;
    qint64 m_maxHeaderBytes = kDefaultMaxHeaderBytes;
    qint64 m_maxBodyBytes = kDefaultMaxBodyBytes;
};

} // namespace LLMCore
