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

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;

    QFuture<QList<QString>> listModels() override;
};

} // namespace LLMQore
