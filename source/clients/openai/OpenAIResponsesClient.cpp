// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OpenAIResponsesClient.hpp>

#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/SSEParser.hpp>

#include <algorithm>

#include <QJsonArray>

#include "OpenAIResponsesMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/Log.hpp>

#include "core/ThreadAffinity.hpp"

namespace LLMQore {

namespace {

const UsageSchema kResponsesUsage{
    QLatin1String("usage"),
    {{}, QLatin1String("input_tokens")},
    {{}, QLatin1String("output_tokens")},
    {QLatin1String("input_tokens_details"), QLatin1String("cached_tokens")},
    {QLatin1String("output_tokens_details"), QLatin1String("reasoning_tokens")}};

} // namespace

OpenAIResponsesClient::OpenAIResponsesClient(QObject *parent)
    : OpenAIResponsesClient({}, {}, {}, parent)
{}

OpenAIResponsesClient::OpenAIResponsesClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OpenAIResponsesClient(url, apiKey, model, nullptr, parent)
{}

OpenAIResponsesClient::OpenAIResponsesClient(
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

RequestID OpenAIResponsesClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    if (m_reasoningPersistence == ReasoningPersistence::Replay && !request.contains("store"))
        request["store"] = false;

    RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/responses") : endpoint;

    qCDebug(llmOpenAILog).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

RequestID OpenAIResponsesClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["model"] = m_model;
    payload["input"] = prompt;

    return sendMessage(payload, {}, mode);
}

QJsonObject OpenAIResponsesClient::buildConversationPayload(
    const Conversation &conversation) const
{
    QJsonObject payload;
    payload["model"] = m_model;

    if (!conversation.system().isEmpty())
        payload["instructions"] = conversation.system();

    QJsonArray input;
    for (const Turn &turn : conversation.turns()) {
        if (turn.role == TurnRole::Tool) {
            for (const TurnContent &block : turn.content) {
                const auto *result = std::get_if<ToolResultContent>(&block);
                if (!result)
                    continue;

                const ToolResult toolResult = toToolResult(*result);

                QJsonObject item = {
                    {"type", "function_call_output"}, {"call_id", result->toolUseId}};

                if (toolResult.hasOnlyText()) {
                    item["output"] = toolResultText(toolResult);
                } else {
                    QJsonArray blocks;
                    for (const ToolContent &part : toolResult.content)
                        blocks.append(OpenAIResponsesMessage::toResponsesInnerBlock(part));
                    item["output"] = blocks;
                }

                input.append(item);
            }
            continue;
        }

        const QList<QJsonObject> items = OpenAIResponsesMessage::serializeTurn(
            turn.role, turn.content, m_reasoningPersistence);
        for (const QJsonObject &item : items)
            input.append(item);
    }

    payload["input"] = input;
    return payload;
}

const ToolDialect &OpenAIResponsesClient::toolDialect() const
{
    return OpenAIResponsesMessage::toolDialect();
}

const UsageSchema &OpenAIResponsesClient::usageSchema() const
{
    return kResponsesUsage;
}

QFuture<QList<ModelInfo>> OpenAIResponsesClient::listModels(const QString &endpoint)
{
    return fetchModelList(endpointUrl(endpoint, QStringLiteral("/models")));
}

QList<BaseClient::ErrorAnnotation> OpenAIResponsesClient::errorAnnotations() const
{
    return {
        {QStringLiteral("type"), QStringLiteral("type")},
        {QStringLiteral("code"), QStringLiteral("code")}};
}

void OpenAIResponsesClient::setReasoningPersistence(ReasoningPersistence mode)
{
    m_reasoningPersistence = mode;
}

OpenAIResponsesClient::ReasoningPersistence
OpenAIResponsesClient::reasoningPersistence() const noexcept
{
    return m_reasoningPersistence;
}

QJsonObject OpenAIResponsesClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    auto *responsesMsg = qobject_cast<OpenAIResponsesMessage *>(message);
    if (!responsesMsg)
        return originalPayload;

    QJsonObject request = originalPayload;
    QJsonArray input = request["input"].toArray();

    QList<QJsonObject> assistantItems = responsesMsg->toItemsFormat(m_reasoningPersistence);
    for (const QJsonObject &item : assistantItems)
        input.append(item);

    QJsonArray toolResultItems = responsesMsg->createToolResultItems(toolResults);
    for (const QJsonValue &item : toolResultItems)
        input.append(item);

    request["input"] = input;
    return request;
}

void OpenAIResponsesClient::processSseEvent(
    const RequestID &id, const SSEEvent &event, const QJsonObject &data)
{
    auto *message = ensureMessage<OpenAIResponsesMessage>(id);
    applyEffects(id, message->applyEvent(event.type, data));
}

void OpenAIResponsesClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    applyEffects(id, ensureMessage<OpenAIResponsesMessage>(id)->applyResponse(response));
}

} // namespace LLMQore
