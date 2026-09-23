// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OpenAIClient.hpp>

#include <QJsonArray>
#include <QJsonValue>

#include "OpenAIMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

#include "core/ThreadAffinity.hpp"

namespace LLMQore {

ProviderProfile openAiProfile()
{
    return ProviderProfile{
        QStringLiteral("/chat/completions"),
        QStringLiteral("/models"),
        &llmOpenAILog(),
        AuthScheme{
            AuthScheme::Placement::Header,
            QStringLiteral("Authorization"),
            QStringLiteral("Bearer ")},
        {{QStringLiteral("Content-Type"), QStringLiteral("application/json")}}};
}

ProviderProfile mistralProfile()
{
    ProviderProfile profile = openAiProfile();
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

OpenAIClient::OpenAIClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : BaseClient(url, apiKey, model, transport, parent)
{
    setProfile(openAiProfile());
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
    return {
        {QStringLiteral("type"), QStringLiteral("type")},
        {QStringLiteral("code"), QStringLiteral("code")}};
}

void OpenAIClient::processSseEvent(
    const RequestID &id, const SSEEvent &, const QJsonObject &chunk)
{
    if (chunk.contains("choices"))
        processStreamChunk(id, chunk);

    applyUsage(id, chunk);
}

QJsonObject OpenAIClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    return appendChatContinuation<OpenAIMessage>(originalPayload, message, toolResults);
}

QString OpenAIClient::takeReasoningAndText(OpenAIMessage *message, const QJsonObject &source)
{
    if (source.contains("reasoning_content") && !source["reasoning_content"].isNull())
        message->handleReasoningDelta(source["reasoning_content"].toString());

    if (!source.contains("content") || source["content"].isNull())
        return {};

    const OpenAIMessage::ContentParts parts = OpenAIMessage::splitContentParts(source["content"]);
    if (!parts.thinking.isEmpty())
        message->handleReasoningDelta(parts.thinking);

    return parts.text;
}

void OpenAIClient::processStreamChunk(const RequestID &id, const QJsonObject &chunk)
{
    QJsonArray choices = chunk["choices"].toArray();
    if (choices.isEmpty())
        return;

    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice["delta"].toObject();
    QString finishReason = choice["finish_reason"].toString();

    OpenAIMessage *message = ensureMessage<OpenAIMessage>(id);

    const QString text = takeReasoningAndText(message, delta);
    if (!text.isEmpty()) {
        notifyPendingThinkingBlocks(id);
        message->handleContentDelta(text);
        addChunk(id, text);
    }

    if (delta.contains("tool_calls")) {
        QJsonArray toolCalls = delta["tool_calls"].toArray();
        for (const auto &toolCallValue : toolCalls) {
            QJsonObject toolCall = toolCallValue.toObject();
            int index = toolCall["index"].toInt();
            QJsonObject function = toolCall["function"].toObject();

            const QString toolCallId = toolCall["id"].toString();
            if (!toolCallId.isEmpty())
                message->handleToolCallStart(index, toolCallId, function["name"].toString());

            if (function.contains("arguments"))
                message->handleToolCallDelta(index, function["arguments"].toString());
        }
    }

    if (!finishReason.isEmpty() && finishReason != "null") {
        notifyPendingThinkingBlocks(id);
        message->completeAllPendingToolCalls();
        message->handleStopReason(finishReason);
        executeToolsFromMessage(id);
    }
}

namespace {

QJsonObject bufferedChoiceAsDelta(const QJsonObject &choice)
{
    QJsonObject delta = choice["message"].toObject();

    QJsonArray toolCalls = delta["tool_calls"].toArray();
    for (int position = 0; position < toolCalls.size(); ++position) {
        QJsonObject call = toolCalls[position].toObject();
        if (!call.contains("index"))
            call["index"] = position;
        toolCalls[position] = call;
    }
    if (!toolCalls.isEmpty())
        delta["tool_calls"] = toolCalls;

    QJsonObject normalized = choice;
    normalized.remove("message");
    normalized["delta"] = delta;
    return normalized;
}

} // namespace

void OpenAIClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    const QJsonArray choices = response["choices"].toArray();
    if (choices.isEmpty()) {
        failRequest(id, QStringLiteral("Empty choices in buffered response"));
        return;
    }

    QJsonObject replayed = response;
    replayed["choices"] = QJsonArray{bufferedChoiceAsDelta(choices.first().toObject())};

    processStreamChunk(id, replayed);
    applyUsage(id, response);
}

} // namespace LLMQore
