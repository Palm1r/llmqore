// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore::Mcp {

class LLMCORE_EXPORT McpTransport : public QObject
{
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Failed };
    Q_ENUM(State)

    explicit McpTransport(QObject *parent = nullptr);
    ~McpTransport() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isOpen() const = 0;
    virtual void send(const QJsonObject &message) = 0;

    State state() const { return m_state; }

signals:
    void messageReceived(const QJsonObject &message);
    void stateChanged(LLMCore::Mcp::McpTransport::State newState);
    void errorOccurred(const QString &reason);
    void closed();
    void stderrLine(const QString &line);

protected:
    void setState(State s);

private:
    State m_state = State::Disconnected;
};

} // namespace LLMCore::Mcp
