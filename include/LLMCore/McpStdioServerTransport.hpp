// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTransport.hpp>

namespace LLMCore::Mcp {

class LLMCORE_EXPORT McpStdioServerTransport : public McpTransport
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

} // namespace LLMCore::Mcp
