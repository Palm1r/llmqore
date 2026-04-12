// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QPair>
#include <QString>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore {

class LLMCORE_EXPORT HttpResponse
{
public:
    int statusCode = 0;

    QList<QPair<QByteArray, QByteArray>> rawHeaders;

    QByteArray body;

    [[nodiscard]] QByteArray rawHeader(QByteArrayView name) const;

    [[nodiscard]] bool hasRawHeader(QByteArrayView name) const;

    [[nodiscard]] QString contentType() const;

    [[nodiscard]] bool isSuccess() const noexcept
    {
        return statusCode >= 200 && statusCode < 300;
    }
};

} // namespace LLMCore
