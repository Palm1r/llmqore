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

void OpenAIResponsesClient::cleanupDerivedData(const RequestID &id)
{
    m_itemIdToCallId.remove(id);
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
    const QString &eventType = event.type;

    OpenAIResponsesMessage *message = ensureMessage<OpenAIResponsesMessage>(id);

    if (eventType == "response.output_text.delta") {
        QString delta = data["delta"].toString();
        if (!delta.isEmpty()) {
            message->handleContentDelta(delta);
            addChunk(id, delta);
        }

    } else if (eventType == "response.output_text.done") {
        QString fullText = data["text"].toString();
        if (!fullText.isEmpty())
            setResponseContent(id, fullText);

    } else if (eventType == "response.output_item.added") {
        QJsonObject item = data["item"].toObject();
        QString itemType = item["type"].toString();

        if (itemType == "function_call") {
            QString callId = item["call_id"].toString();
            QString name = item["name"].toString();
            QString itemId = item["id"].toString();

            if (!callId.isEmpty() && !name.isEmpty()) {
                m_itemIdToCallId[id][itemId] = callId;
                message->handleToolCallStart(callId, name);
            }
        } else if (itemType == "reasoning") {
            QString itemId = item["id"].toString();
            if (!itemId.isEmpty()) {
                message->handleReasoningStart(itemId);
                message->handleReasoningEncryptedContent(
                    itemId, item["encrypted_content"].toString());
            }
        }

    } else if (eventType == "response.reasoning_content.delta") {
        QString itemId = data["item_id"].toString();
        QString delta = data["delta"].toString();
        if (!itemId.isEmpty() && !delta.isEmpty())
            message->handleReasoningDelta(itemId, delta);

    } else if (eventType == "response.reasoning_content.done") {
        QString itemId = data["item_id"].toString();
        if (!itemId.isEmpty()) {
            message->handleReasoningComplete(itemId);
            notifyPendingThinkingBlocks(id);
        }

    } else if (eventType == "response.function_call_arguments.delta") {
        QString itemId = data["item_id"].toString();
        QString delta = data["delta"].toString();
        if (!itemId.isEmpty() && !delta.isEmpty()) {
            QString callId = m_itemIdToCallId.value(id).value(itemId);
            if (!callId.isEmpty())
                message->handleToolCallDelta(callId, delta);
        }

    } else if (
        eventType == "response.function_call_arguments.done"
        || eventType == "response.output_item.done") {
        QString itemId = data["item_id"].toString();
        QJsonObject item = data["item"].toObject();

        if (!item.isEmpty() && item["type"].toString() == "reasoning") {
            QString finalItemId = itemId.isEmpty() ? item["id"].toString() : itemId;
            QString reasoningText = extractReasoningText(item);

            if (reasoningText.isEmpty()) {
                reasoningText = QStringLiteral(
                    "[Reasoning process completed, but detailed thinking is not available "
                    "in streaming mode.]");
            }

            if (!finalItemId.isEmpty()) {
                message->handleReasoningDelta(finalItemId, reasoningText);
                message->handleReasoningEncryptedContent(
                    finalItemId, item["encrypted_content"].toString());
                message->handleReasoningComplete(finalItemId);
                notifyPendingThinkingBlocks(id);
            }
        } else if (item.isEmpty() && !itemId.isEmpty()) {
            QString callId = m_itemIdToCallId.value(id).value(itemId);
            if (!callId.isEmpty()) {
                const QString finalArguments = data["arguments"].toString();
                message->handleToolCallComplete(callId, finalArguments);
            }
        } else if (!item.isEmpty() && item["type"].toString() == "function_call") {
            QString callId = item["call_id"].toString();
            if (!callId.isEmpty()) {
                const QString finalArguments = item["arguments"].toString();
                message->handleToolCallComplete(callId, finalArguments);
            }
        }

    } else if (eventType == "response.completed") {
        QJsonObject responseObj = data["response"].toObject();
        QString statusStr = responseObj["status"].toString();

        if (responseContent(id).isEmpty()) {
            QString aggregatedText = extractAggregatedText(responseObj);
            if (!aggregatedText.isEmpty())
                setResponseContent(id, aggregatedText);
        }

        message->handleStatus(statusStr);

        applyUsage(id, responseObj);

        notifyPendingThinkingBlocks(id);
        executeToolsFromMessage(id);

    } else if (eventType == "response.incomplete") {
        QJsonObject responseObj = data["response"].toObject();

        if (!responseObj.isEmpty()) {
            QString statusStr = responseObj["status"].toString();

            if (responseContent(id).isEmpty()) {
                QString aggregatedText = extractAggregatedText(responseObj);
                if (!aggregatedText.isEmpty())
                    setResponseContent(id, aggregatedText);
            }

            message->handleStatus(statusStr);
        } else {
            message->handleStatus("incomplete");
        }

        notifyPendingThinkingBlocks(id);
        executeToolsFromMessage(id);
    }
}

QString OpenAIResponsesClient::extractAggregatedText(const QJsonObject &responseObj)
{
    if (responseObj.contains("output_text")) {
        QString outputText = responseObj["output_text"].toString();
        if (!outputText.isEmpty())
            return outputText;
    }

    QString aggregated;
    if (responseObj.contains("output")) {
        QJsonArray output = responseObj["output"].toArray();
        for (const auto &item : output) {
            QJsonObject itemObj = item.toObject();
            if (itemObj["type"].toString() == "message" && itemObj.contains("content")) {
                QJsonArray content = itemObj["content"].toArray();
                for (const auto &contentItem : content) {
                    QJsonObject contentObj = contentItem.toObject();
                    if (contentObj["type"].toString() == "output_text")
                        aggregated += contentObj["text"].toString();
                }
            }
        }
    }
    return aggregated;
}

QString OpenAIResponsesClient::extractReasoningText(const QJsonObject &item)
{
    QString reasoningText;

    if (item.contains("summary")) {
        QJsonArray summary = item["summary"].toArray();
        for (const auto &summaryItem : summary) {
            QJsonObject summaryObj = summaryItem.toObject();
            if (summaryObj["type"].toString() == "summary_text") {
                reasoningText = summaryObj["text"].toString();
                break;
            }
        }
    }

    if (reasoningText.isEmpty() && item.contains("content")) {
        QJsonArray content = item["content"].toArray();
        QStringList texts;
        for (const auto &contentItem : content) {
            QJsonObject contentObj = contentItem.toObject();
            if (contentObj["type"].toString() == "reasoning_text")
                texts.append(contentObj["text"].toString());
        }
        if (!texts.isEmpty())
            reasoningText = texts.join("\n");
    }

    return reasoningText;
}

void OpenAIResponsesClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    auto *message = ensureMessage<OpenAIResponsesMessage>(id);

    QJsonArray output = response["output"].toArray();
    for (const auto &item : output) {
        QJsonObject itemObj = item.toObject();
        QString itemType = itemObj["type"].toString();

        if (itemType == "reasoning") {
            QString itemId = itemObj["id"].toString();
            QString reasoningText = extractReasoningText(itemObj);

            if (!itemId.isEmpty() && !reasoningText.isEmpty()) {
                message->handleReasoningStart(itemId);
                message->handleReasoningDelta(itemId, reasoningText);
                message->handleReasoningComplete(itemId);
            }

        } else if (itemType == "message") {
            QJsonArray content = itemObj["content"].toArray();
            for (const auto &contentItem : content) {
                QJsonObject contentObj = contentItem.toObject();
                if (contentObj["type"].toString() == "output_text") {
                    QString text = contentObj["text"].toString();
                    if (!text.isEmpty()) {
                        message->handleContentDelta(text);
                        addChunk(id, text);
                    }
                }
            }

        } else if (itemType == "function_call") {
            QString callId = itemObj["call_id"].toString();
            QString name = itemObj["name"].toString();
            QString arguments = itemObj["arguments"].toString();

            if (!callId.isEmpty() && !name.isEmpty()) {
                message->handleToolCallStart(callId, name);
                if (!arguments.isEmpty())
                    message->handleToolCallDelta(callId, arguments);
                message->handleToolCallComplete(callId);
            }
        }
    }

    notifyPendingThinkingBlocks(id);

    QString status = response["status"].toString();
    if (!status.isEmpty()) {
        message->handleStatus(status);
        executeToolsFromMessage(id);
    }

    applyUsage(id, response);
}

} // namespace LLMQore
