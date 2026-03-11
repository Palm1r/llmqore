// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore {

class LLMCORE_EXPORT SSEBuffer
{
public:
    SSEBuffer() = default;

    QStringList processData(const QByteArray &data);

    void clear();
    QString currentBuffer() const;
    bool hasIncompleteData() const;

private:
    QString m_buffer;
};

} // namespace LLMCore
