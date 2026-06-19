// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Rpc {

class LLMQORE_EXPORT Transport : public QObject
{
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected, Failed };
    Q_ENUM(State)

    explicit Transport(QObject *parent = nullptr);
    ~Transport() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isOpen() const = 0;
    virtual void send(const QJsonObject &message) = 0;

    State state() const { return m_state; }

signals:
    void messageReceived(const QJsonObject &message);
    void stateChanged(LLMQore::Rpc::Transport::State newState);
    void errorOccurred(const QString &reason);
    void closed();
    void stderrLine(const QString &line);

protected:
    void setState(State s);

private:
    State m_state = State::Disconnected;
};

} // namespace LLMQore::Rpc
