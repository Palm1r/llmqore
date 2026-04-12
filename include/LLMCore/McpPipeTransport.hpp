// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <utility>

#include <QPointer>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTransport.hpp>

namespace LLMCore::Mcp {

class LLMCORE_EXPORT McpPipeTransport : public McpTransport
{
    Q_OBJECT
public:
    ~McpPipeTransport() override;

    static std::pair<McpPipeTransport *, McpPipeTransport *> createPair(
        QObject *parent = nullptr);

    void start() override;
    void stop() override;
    bool isOpen() const override { return m_open; }
    void send(const QJsonObject &message) override;

private slots:
    void deliver(const QJsonObject &message);

private:
    explicit McpPipeTransport(QObject *parent = nullptr);

    QPointer<McpPipeTransport> m_peer;
    bool m_open = false;
};

} // namespace LLMCore::Mcp
