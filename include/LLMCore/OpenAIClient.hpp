// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QUrl>

#include <LLMCore/BaseClient.hpp>

namespace LLMCore {

class OpenAIMessage;

class LLMCORE_EXPORT OpenAIClient : public BaseClient
{
    Q_OBJECT
public:
    explicit OpenAIClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);

    RequestID sendMessage(
        const QJsonObject &payload,
        RequestCallbacks callbacks = {},
        RequestMode mode = RequestMode::Streaming) override;
    RequestID ask(
        const QString &prompt,
        RequestCallbacks callbacks = {},
        RequestMode mode = RequestMode::Streaming) override;
    ToolSchemaFormat toolSchemaFormat() const override { return ToolSchemaFormat::OpenAI; }

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
        const QHash<QString, QString> &toolResults) override;

private:
    void processStreamChunk(const RequestID &id, const QJsonObject &chunk);

    QHash<RequestID, OpenAIMessage *> m_messages;
};

} // namespace LLMCore
