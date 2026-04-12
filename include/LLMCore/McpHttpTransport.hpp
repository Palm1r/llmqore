// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <QHash>
#include <QString>
#include <QUrl>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTransport.hpp>

namespace LLMCore {
class HttpClient;
}

namespace LLMCore::Mcp {

enum class McpHttpSpec {
    V2024_11_05,
    V2025_03_26,
    Latest = V2025_03_26,
};

struct LLMCORE_EXPORT HttpTransportConfig
{
    QUrl endpoint;
    McpHttpSpec spec = McpHttpSpec::Latest;
    QHash<QString, QString> headers;
    int requestTimeoutMs = 120000;
};

class LLMCORE_EXPORT McpHttpTransport : public McpTransport
{
    Q_OBJECT
public:
    explicit McpHttpTransport(
        HttpTransportConfig config,
        LLMCore::HttpClient *httpClient = nullptr,
        QObject *parent = nullptr);
    ~McpHttpTransport() override;

    void start() override;
    void stop() override;
    bool isOpen() const override;
    void send(const QJsonObject &message) override;

    const HttpTransportConfig &config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMCore::Mcp
