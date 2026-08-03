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
    RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming) override;
    using BaseClient::ask;

    QFuture<QList<ModelInfo>> listModels(const QString &endpoint = {}) override;
    QJsonObject buildConversationPayload(const Conversation &conversation) const override;

protected:
    [[nodiscard]] const ToolDialect &toolDialect() const override;
    [[nodiscard]] const UsageSchema &usageSchema() const override;
    void processData(const RequestID &id, const QByteArray &data) override;
    void processBufferedBody(const RequestID &id, const QJsonObject &body) override;
    void flushStreamBuffers(const RequestID &id) override;
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults) override;

private:
    void processStreamData(const RequestID &id, const QJsonObject &data);
    // False when the object was a provider error and the request was failed.
    bool handleStreamObject(const RequestID &id, const QJsonObject &obj);

};

} // namespace LLMQore
