// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QJsonObject>
#include <QJsonValue>

namespace LLMQore {

[[nodiscard]] inline std::optional<QJsonObject> errorIn(const QJsonObject &body)
{
    const QJsonValue error = body.value(QLatin1String("error"));
    if (error.isObject())
        return error.toObject();
    if (error.isString() && !error.toString().isEmpty())
        return QJsonObject{{QStringLiteral("message"), error.toString()}};
    return std::nullopt;
}

} // namespace LLMQore
