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
    setLogCategory(llmOpenAILog());
    setAuthScheme(
        {.placement = AuthScheme::Placement::Header,
         .name = QStringLiteral("Authorization"),
         .valuePrefix = QStringLiteral("Bearer ")});
    setHeaders({{QStringLiteral("Content-Type"), QStringLiteral("application/json")}});
}

QJsonObject OpenAIClient::buildConversationPayload(const Conversation &conversation) const
{
    QJsonObject payload;
    payload["model"] = m_model;

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
    LLMQORE_ASSERT_OWNING_THREAD();
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    if (mode == RequestMode::Streaming) {
        QJsonObject streamOptions = request.value("stream_options").toObject();
        streamOptions["include_usage"] = true;
        request["stream_options"] = streamOptions;
    }

    RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/chat/completions") : endpoint;

    qCDebug(logCategory()).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

RequestID OpenAIClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["model"] = m_model;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}};

    return sendMessage(payload, {}, mode);
}

QFuture<QList<ModelInfo>> OpenAIClient::listModels(const QString &endpoint)
{
    return fetchModelList(endpointUrl(endpoint, QStringLiteral("/models")));
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
        message->handleFinishReason(finishReason);
        executeToolsFromMessage(id);
    }
}

void OpenAIClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    QJsonArray choices = response["choices"].toArray();
    if (choices.isEmpty()) {
        failRequest(id, QStringLiteral("Empty choices in buffered response"));
        return;
    }

    QJsonObject choice = choices[0].toObject();
    QJsonObject messageObj = choice["message"].toObject();
    QString finishReason = choice["finish_reason"].toString();

    auto *message = ensureMessage<OpenAIMessage>(id);

    const QString text = takeReasoningAndText(message, messageObj);
    if (!text.isEmpty()) {
        message->handleContentDelta(text);
        addChunk(id, text);
    }

    notifyPendingThinkingBlocks(id);

    if (messageObj.contains("tool_calls")) {
        QJsonArray toolCalls = messageObj["tool_calls"].toArray();
        for (int i = 0; i < toolCalls.size(); ++i) {
            QJsonObject toolCall = toolCalls[i].toObject();
            QString toolId = toolCall["id"].toString();
            QJsonObject function = toolCall["function"].toObject();
            QString name = function["name"].toString();
            QString arguments = function["arguments"].toString();

            message->handleToolCallStart(i, toolId, name);
            message->handleToolCallDelta(i, arguments);
            message->handleToolCallComplete(i);
        }
    }

    if (!finishReason.isEmpty()) {
        message->handleFinishReason(finishReason);
        executeToolsFromMessage(id);
    }

    applyUsage(id, response);
}

} // namespace LLMQore
