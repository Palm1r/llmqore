// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <utility>

#include <QPointer>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/RpcTransport.hpp>

namespace LLMQore::Rpc {

class LLMQORE_EXPORT PipeTransport : public Transport
{
    Q_OBJECT
public:
    ~PipeTransport() override;

    static std::pair<PipeTransport *, PipeTransport *> createPair(QObject *parent = nullptr);

    void start() override;
    void stop() override;
    bool isOpen() const override { return m_open; }
    void send(const QJsonObject &message) override;

private slots:
    void deliver(const QJsonObject &message);

private:
    explicit PipeTransport(QObject *parent = nullptr);

    QPointer<PipeTransport> m_peer;
    bool m_open = false;
};

} // namespace LLMQore::Rpc
