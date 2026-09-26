// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QUrl>

#include <LLMQore/BaseClient.hpp>

namespace LLMQore {

class OllamaMessage;

[[nodiscard]] LLMQORE_EXPORT ProviderProfile ollamaProfile();

class LLMQORE_EXPORT OllamaClient : public BaseClient
{
    Q_OBJECT
public:
    explicit OllamaClient(QObject *parent = nullptr);
    explicit OllamaClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit OllamaClient(
        const QString &url,
        const QString &apiKey,
        const QString &model,
        HttpTransport *transport,
        QObject *parent = nullptr);

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;

    QFuture<QList<ModelInfo>> listModels(const QString &endpoint = {}) override;
    QJsonObject buildConversationPayload(const Conversation &conversation) const override;

protected:
    [[nodiscard]] const ToolDialect &toolDialect() const override;
    [[nodiscard]] const UsageSchema &usageSchema() const override;
    [[nodiscard]] StreamFraming streamFraming() const override;
    void processJsonLine(const RequestID &id, const QJsonObject &json) override;
    void processBufferedBody(const RequestID &id, const QJsonObject &body) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
};

} // namespace LLMQore
