// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/LlamaCppClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>

#include "core/ThreadAffinity.hpp"

namespace LLMQore {

namespace {

const UsageSchema kLlamaCppNativeUsage{
    {},
    {{}, QLatin1String("tokens_evaluated")},
    {{}, QLatin1String("tokens_predicted")},
    {},
    {}};

} // namespace

LlamaCppClient::LlamaCppClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : OpenAIClient(url, apiKey, model, transport, parent)
{
    ProviderProfile profile = openAiProfile();
    profile.log = &llmLlamaCppLog();
    profile.chatPath = QStringLiteral("/v1/chat/completions");
    profile.modelsPath = QStringLiteral("/v1/models");
    setProfile(profile);
}

QFuture<bool> LlamaCppClient::isServerReady()
{
    const QUrl target(url() + "/health");
    QNetworkRequest request = prepareNetworkRequest(target);

    return LLMQore::compat(transport()->send(request, QByteArrayView("GET")))
        .then(this, [](const HttpResponse &response) {
            if (!response.isSuccess())
                return false;
            QJsonObject json = QJsonDocument::fromJson(response.body).object();
            return json["status"].toString() == "ok";
        })
        .onFailed(this, [](const std::exception &) { return false; });
}

QFuture<QJsonObject> LlamaCppClient::serverProps()
{
    const QUrl target(url() + "/props");
    QNetworkRequest request = prepareNetworkRequest(target);

    return LLMQore::compat(transport()->send(request, QByteArrayView("GET")))
        .then(this, [](const HttpResponse &response) -> QJsonObject {
            if (!response.isSuccess())
                return {};
            return QJsonDocument::fromJson(response.body).object();
        })
        .onFailed(this, [](const std::exception &) { return QJsonObject{}; });
}

bool LlamaCppClient::isNativeCompletionChunk(const QJsonObject &chunk)
{
    return chunk.contains("content") && !chunk.contains("choices");
}

void LlamaCppClient::processSseEvent(
    const RequestID &id, const SSEEvent &event, const QJsonObject &chunk)
{
    if (!isNativeCompletionChunk(chunk)) {
        OpenAIClient::processSseEvent(id, event, chunk);
        return;
    }

    const QString content = chunk["content"].toString();
    if (!content.isEmpty())
        addChunk(id, content);

    applyUsage(id, chunk, kLlamaCppNativeUsage);

    if (chunk["stop"].toBool()) {
        cleanupFullRequest(id);
        completeRequest(id);
    }
}

void LlamaCppClient::processBufferedBody(const RequestID &id, const QJsonObject &body)
{
    if (!isNativeCompletionChunk(body)) {
        OpenAIClient::processBufferedBody(id, body);
        return;
    }

    const QString content = body["content"].toString();
    if (!content.isEmpty())
        addChunk(id, content);

    applyUsage(id, body, kLlamaCppNativeUsage);

    cleanupFullRequest(id);
    completeRequest(id);
}

} // namespace LLMQore
