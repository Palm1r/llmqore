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
    explicit Transport(QObject *parent = nullptr);
    ~Transport() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isOpen() const = 0;
    virtual void send(const QJsonObject &message) = 0;

signals:
    void messageReceived(const QJsonObject &message);
    void errorOccurred(const QString &reason);
    void sendFailed(const QJsonObject &message, const QString &reason);
    void closed();
};

} // namespace LLMQore::Rpc
