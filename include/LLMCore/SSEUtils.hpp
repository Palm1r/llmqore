// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace LLMCore::SSEUtils {

inline QJsonObject parseEventLine(const QString &line)
{
    if (!line.startsWith("data: "))
        return QJsonObject();

    QStringView payload = QStringView(line).mid(6);
    if (payload == u"[DONE]")
        return QJsonObject();

    QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
    return doc.object();
}

} // namespace LLMCore::SSEUtils
