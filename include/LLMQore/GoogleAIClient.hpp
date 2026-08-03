// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

class GoogleMessage;

[[nodiscard]] LLMQORE_EXPORT ProviderProfile googleProfile();

class LLMQORE_EXPORT GoogleAIClient : public BaseClient
{
    Q_OBJECT
public:
    explicit GoogleAIClient(
        const QString &url = {},
        const QString &apiKey = {},
        const QString &model = {},
        HttpTransport *transport = nullptr,
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
    void processData(const RequestID &id, const QByteArray &data) override;
    void processSseEvent(
        const RequestID &id, const SSEEvent &event, const QJsonObject &json) override;
    void processBufferedBody(const RequestID &id, const QJsonObject &body) override;
    std::optional<QString> takePendingStreamError(const RequestID &id) override;
    void onStreamDrained(const RequestID &id) override;
    void cleanupDerivedData(const RequestID &id) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QList<ErrorAnnotation> errorAnnotations() const override;

private:
    class JsonErrorSniffer
    {
    public:
        static constexpr qsizetype kMaxBytes = 64 * 1024;

        std::optional<QString> append(const QByteArray &chunk);

    private:
        bool m_active = true;
        QByteArray m_buffer;
    };

    void processStreamChunk(const RequestID &id, const QJsonObject &chunk);

    QHash<RequestID, QString> m_failedRequests;
    QHash<RequestID, JsonErrorSniffer> m_errorSniffers;
};

} // namespace LLMQore
