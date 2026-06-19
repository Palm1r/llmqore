// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QObject>
#include <QString>

#include <LLMQore/AcpTypes.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Acp {

class LLMQORE_EXPORT AcpTerminalProvider : public QObject
{
    Q_OBJECT
public:
    explicit AcpTerminalProvider(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~AcpTerminalProvider() override = default;

    virtual QFuture<CreateTerminalResult> createTerminal(const CreateTerminalParams &params) = 0;
    virtual QFuture<TerminalOutputResult> terminalOutput(
        const QString &sessionId, const QString &terminalId) = 0;
    virtual QFuture<WaitForTerminalExitResult> waitForExit(
        const QString &sessionId, const QString &terminalId) = 0;
    virtual QFuture<void> killTerminal(const QString &sessionId, const QString &terminalId) = 0;
    virtual QFuture<void> releaseTerminal(const QString &sessionId, const QString &terminalId) = 0;
};

} // namespace LLMQore::Acp
