// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QUrl>

#include <QLoggingCategory>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

class OpenAIMessage;

class LLMQORE_EXPORT OpenAIClient : public BaseClient
{
    Q_OBJECT
public:
    explicit OpenAIClient(QObject *parent = nullptr);
    explicit OpenAIClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit OpenAIClient(
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

protected:
    [[nodiscard]] const ToolDialect &toolDialect() const override;
    [[nodiscard]] const UsageSchema &usageSchema() const override;
    void processBufferedBody(const RequestID &id, const QJsonObject &body) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QList<ErrorAnnotation> errorAnnotations() const override;

    void processSseEvent(
        const RequestID &id, const SSEEvent &event, const QJsonObject &json) override;

private:
    static QString takeReasoningAndText(OpenAIMessage *message, const QJsonObject &source);
    void processStreamChunk(const RequestID &id, const QJsonObject &chunk);

};

} // namespace LLMQore
