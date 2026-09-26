// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OpenAIClient.hpp>

#include <QJsonArray>
#include <QJsonValue>

#include "OpenAIErrorAnnotations.hpp"
#include "OpenAIMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

ProviderProfile openAIProfile()
{
    ProviderProfile profile = {
        .chatPath = QStringLiteral("/chat/completions"),
        .modelsPath = QStringLiteral("/models"),
        .log = &llmOpenAILog()};
    profile.auth = {
        .placement = AuthScheme::Placement::Header,
        .name = QStringLiteral("Authorization"),
        .valuePrefix = QStringLiteral("Bearer ")};
    return profile;
}

ProviderProfile mistralProfile()
{
    ProviderProfile profile = openAIProfile();
    profile.chatPath = QStringLiteral("/v1/chat/completions");
    profile.modelsPath = QStringLiteral("/v1/models");
    profile.log = &llmMistralLog();
    return profile;
}

namespace {

const UsageSchema kOpenAIUsage{
    QLatin1String("usage"),
    {{}, QLatin1String("prompt_tokens")},
    {{}, QLatin1String("completion_tokens")},
    {QLatin1String("prompt_tokens_details"), QLatin1String("cached_tokens")},
    {QLatin1String("completion_tokens_details"), QLatin1String("reasoning_tokens")}};

} // namespace

OpenAIClient::OpenAIClient(QObject *parent)
    : OpenAIClient({}, {}, {}, parent)
{}

OpenAIClient::OpenAIClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OpenAIClient(url, apiKey, model, nullptr, parent)
{}

OpenAIClient::OpenAIClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : BaseClient(url, apiKey, model, transport, parent)
{
    setProfile(openAIProfile());
}

QJsonObject OpenAIClient::buildConversationPayload(const Conversation &conversation) const
{
    QJsonObject payload;
    payload["model"] = model();

    QJsonArray messages;
    if (!conversation.system().isEmpty())
        messages.append(QJsonObject{{"role", "system"}, {"content", conversation.system()}});

    for (const Turn &turn : conversation.turns()) {
        if (turn.role == TurnRole::Tool) {
            for (const TurnContent &block : turn.content) {
                if (const auto *result = std::get_if<ToolResultContent>(&block)) {
                    messages.append(
                        QJsonObject{
                            {"role", "tool"},
                            {"tool_call_id", result->toolUseId},
                            {"content", toolResultText(toToolResult(*result))}});
                }
            }
            continue;
        }

        messages.append(OpenAIMessage::serializeTurn(turn.role, turn.content));
    }

    payload["messages"] = messages;
    return payload;
}

const ToolDialect &OpenAIClient::toolDialect() const
{
    return OpenAIMessage::toolDialect();
}

const UsageSchema &OpenAIClient::usageSchema() const
{
    return kOpenAIUsage;
}

RequestID OpenAIClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    if (mode == RequestMode::Streaming) {
        QJsonObject streamOptions = request.value("stream_options").toObject();
        streamOptions["include_usage"] = true;
        request["stream_options"] = streamOptions;
    }

    return postJson(request, endpoint, mode);
}

QFuture<QList<ModelInfo>> OpenAIClient::listModels(const QString &endpoint)
{
    return fetchModelList(endpointUrl(endpoint, profile().modelsPath));
}

QList<BaseClient::ErrorAnnotation> OpenAIClient::errorAnnotations() const
{
    return openAIErrorAnnotations();
}

void OpenAIClient::processSseEvent(
    const RequestID &id, const SSEEvent &, const QJsonObject &chunk)
{
    auto *message = qobject_cast<OpenAIMessage *>(messageForRequest(id));
    if (!message || !chunk.value("choices").toArray().isEmpty())
        message = ensureMessage<OpenAIMessage>(id);

    applyEffects(id, message->applyEvent(chunk));
}

QJsonObject OpenAIClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    return appendChatContinuation<OpenAIMessage>(originalPayload, message, toolResults);
}

void OpenAIClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    applyEffects(id, ensureMessage<OpenAIMessage>(id)->applyResponse(response));
}

} // namespace LLMQore
