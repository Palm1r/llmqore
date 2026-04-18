// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QUrl>

#include <LLMQore/BaseClient.hpp>

namespace LLMQore {

class OpenAIResponsesMessage;

class LLMQORE_EXPORT OpenAIResponsesClient : public BaseClient
{
    Q_OBJECT
public:
    explicit OpenAIResponsesClient(QObject *parent = nullptr);
    explicit OpenAIResponsesClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;
    RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming) override;
    ToolSchemaFormat toolSchemaFormat() const override { return ToolSchemaFormat::OpenAIResponses; }

    QFuture<QList<QString>> listModels() override;

protected:
    QNetworkRequest prepareNetworkRequest(const QUrl &url) const override;
    void processData(const RequestID &id, const QByteArray &data) override;
    void processBufferedResponse(const RequestID &id, const QByteArray &data) override;
    BaseMessage *messageForRequest(const RequestID &id) const override;
    void cleanupDerivedData(const RequestID &id) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QString parseHttpError(const HttpResponse &response) const override;

private:
    void processStreamEvent(const RequestID &id, const QString &eventType, const QJsonObject &data);

    static QString extractAggregatedText(const QJsonObject &responseObj);
    static QString extractReasoningText(const QJsonObject &item);

    QHash<RequestID, OpenAIResponsesMessage *> m_messages;
    QHash<RequestID, QHash<QString, QString>> m_itemIdToCallId;
};

} // namespace LLMQore
