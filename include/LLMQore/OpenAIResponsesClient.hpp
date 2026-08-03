// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QUrl>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

class OpenAIResponsesMessage;

enum class ReasoningPersistence { Off, Replay };

class LLMQORE_EXPORT OpenAIResponsesClient : public BaseClient
{
    Q_OBJECT
public:
    explicit OpenAIResponsesClient(QObject *parent = nullptr);
    explicit OpenAIResponsesClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit OpenAIResponsesClient(
        const QString &url,
        const QString &apiKey,
        const QString &model,
        HttpTransport *transport,
        QObject *parent = nullptr);

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;
    RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming) override;
    using BaseClient::ask;

    QFuture<QList<ModelInfo>> listModels(const QString &endpoint = {}) override;
    QJsonObject buildConversationPayload(const Conversation &conversation) const override;

    using ReasoningPersistence = LLMQore::ReasoningPersistence;

    void setReasoningPersistence(ReasoningPersistence mode);
    [[nodiscard]] ReasoningPersistence reasoningPersistence() const noexcept;

protected:
    [[nodiscard]] const ToolDialect &toolDialect() const override;
    [[nodiscard]] const UsageSchema &usageSchema() const override;
    void processSseEvent(
        const RequestID &id, const SSEEvent &event, const QJsonObject &json) override;
    void processBufferedBody(const RequestID &id, const QJsonObject &body) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QList<ErrorAnnotation> errorAnnotations() const override;

private:
    ReasoningPersistence m_reasoningPersistence = ReasoningPersistence::Off;
};

} // namespace LLMQore
