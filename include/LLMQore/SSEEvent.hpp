// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QString>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

struct LLMQORE_EXPORT SSEEvent
{
    QString type;
    QByteArray data;
    QByteArray id;
};

} // namespace LLMQore
