// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>

#include <LLMQore/AcpTerminalProvider.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Acp {

class LLMQORE_EXPORT TerminalManager : public AcpTerminalProvider
{
    Q_OBJECT
public:
    explicit TerminalManager(QObject *parent = nullptr);
    ~TerminalManager() override;

    QFuture<CreateTerminalResult> createTerminal(const CreateTerminalParams &params) override;
    QFuture<TerminalOutputResult> terminalOutput(
        const QString &sessionId, const QString &terminalId) override;
    QFuture<WaitForTerminalExitResult> waitForExit(
        const QString &sessionId, const QString &terminalId) override;
    QFuture<void> killTerminal(const QString &sessionId, const QString &terminalId) override;
    QFuture<void> releaseTerminal(const QString &sessionId, const QString &terminalId) override;

private:
    struct Terminal;
    void onFinished(Terminal *t, int exitCode, int exitStatus);
    void appendOutput(Terminal *t, const QByteArray &data);

    QHash<QString, Terminal *> m_terminals;
    int m_nextId = 1;
};

} // namespace LLMQore::Acp
