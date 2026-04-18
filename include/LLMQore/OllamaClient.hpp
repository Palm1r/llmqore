// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QUrl>

#include <LLMQore/BaseClient.hpp>

namespace LLMQore {

class OllamaMessage;

class LLMQORE_EXPORT OllamaClient : public BaseClient
{
    Q_OBJECT
public:
    explicit OllamaClient(QObject *parent = nullptr);
    explicit OllamaClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;
    RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming) override;
    ToolSchemaFormat toolSchemaFormat() const override { return ToolSchemaFormat::Ollama; }

    QFuture<QList<QString>> listModels() override;

protected:
    QNetworkRequest prepareNetworkRequest(const QUrl &url) const override;
    void processData(const RequestID &id, const QByteArray &data) override;
    void processBufferedResponse(const RequestID &id, const QByteArray &data) override;
    void onStreamFinished(const RequestID &id, std::optional<QString> error) override;
    BaseMessage *messageForRequest(const RequestID &id) const override;
    void cleanupDerivedData(const RequestID &id) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;
    [[nodiscard]] QString parseHttpError(const HttpResponse &response) const override;

private:
    void processStreamData(const RequestID &id, const QJsonObject &data);
    void notifyThinkingBlocks(const RequestID &id, OllamaMessage *message);

    QHash<RequestID, OllamaMessage *> m_messages;
    QSet<RequestID> m_thinkingEmitted;
};

} // namespace LLMQore
