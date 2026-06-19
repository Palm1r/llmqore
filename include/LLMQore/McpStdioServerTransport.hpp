// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/RpcTransport.hpp>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT McpStdioServerTransport : public Rpc::Transport
{
    Q_OBJECT
public:
    explicit McpStdioServerTransport(QObject *parent = nullptr);
    ~McpStdioServerTransport() override;

    void start() override;
    void stop() override;
    bool isOpen() const override;
    void send(const QJsonObject &message) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore::Mcp
