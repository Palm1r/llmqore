// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/OpenAIClient.hpp>

namespace LLMQore {

class LLMQORE_EXPORT MistralClient : public OpenAIClient
{
    Q_OBJECT
public:
    explicit MistralClient(QObject *parent = nullptr);
    explicit MistralClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);

    // Mistral exposes /chat/completions (default) and /fim/completions
    // (Codestral models). Pass `/fim/completions` explicitly to target FIM.
    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;
};

} // namespace LLMQore
