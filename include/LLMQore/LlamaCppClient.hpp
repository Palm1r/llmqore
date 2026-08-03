// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QJsonObject>
#include <QUrl>

#include <LLMQore/OpenAIClient.hpp>

namespace LLMQore {

class LLMQORE_EXPORT LlamaCppClient : public OpenAIClient
{
    Q_OBJECT
public:
    explicit LlamaCppClient(
        const QString &url = {},
        const QString &apiKey = {},
        const QString &model = {},
        HttpTransport *transport = nullptr,
        QObject *parent = nullptr);

    using OpenAIClient::ask;

    QFuture<bool> isServerReady();
    QFuture<QJsonObject> serverProps();

protected:
    void processBufferedBody(const RequestID &id, const QJsonObject &body) override;
    void processSseEvent(
        const RequestID &id, const SSEEvent &event, const QJsonObject &json) override;

private:
    static bool isNativeCompletionChunk(const QJsonObject &chunk);
};

} // namespace LLMQore
