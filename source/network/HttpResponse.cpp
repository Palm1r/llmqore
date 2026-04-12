// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/HttpResponse.hpp>

namespace LLMCore {

namespace {

bool headerNameMatches(const QByteArray &have, QByteArrayView want) noexcept
{
    return QByteArrayView(have).compare(want, Qt::CaseInsensitive) == 0;
}

} // namespace

QByteArray HttpResponse::rawHeader(QByteArrayView name) const
{
    for (const auto &pair : rawHeaders) {
        if (headerNameMatches(pair.first, name))
            return pair.second;
    }
    return {};
}

bool HttpResponse::hasRawHeader(QByteArrayView name) const
{
    for (const auto &pair : rawHeaders) {
        if (headerNameMatches(pair.first, name))
            return true;
    }
    return false;
}

QString HttpResponse::contentType() const
{
    const QByteArray value = rawHeader(QByteArrayView("Content-Type"));
    if (value.isEmpty())
        return {};
    return QString::fromUtf8(value).toLower();
}

} // namespace LLMCore
