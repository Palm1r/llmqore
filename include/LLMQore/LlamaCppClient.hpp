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

class OpenAIMessage;

class LLMQORE_EXPORT LlamaCppClient : public BaseClient
{
    Q_OBJECT
public:
    explicit LlamaCppClient(QObject *parent = nullptr);
    explicit LlamaCppClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;
    RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming) override;
    ToolSchemaFormat toolSchemaFormat() const override { return ToolSchemaFormat::OpenAI; }

    QFuture<QList<QString>> listModels() override;

    QFuture<bool> isServerReady();
    QFuture<QJsonObject> serverProps();

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
    void onStreamFinished(const RequestID &id, std::optional<QString> error) override;
    [[nodiscard]] QString parseHttpError(const HttpResponse &response) const override;

private:
    void processStreamChunk(const RequestID &id, const QJsonObject &chunk);
    void emitPendingThinking(const RequestID &id);

    QHash<RequestID, OpenAIMessage *> m_messages;
    QHash<RequestID, QString> m_reasoningContent;
    QSet<RequestID> m_thinkingEmitted;
};

} // namespace LLMQore
