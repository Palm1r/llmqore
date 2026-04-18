// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OpenAIResponsesClient.hpp>

#include <LLMQore/HttpClient.hpp>
#include <LLMQore/SSEParser.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "OpenAIResponsesMessage.hpp"
#include <LLMQore/Log.hpp>

namespace LLMQore {

OpenAIResponsesClient::OpenAIResponsesClient(QObject *parent)
    : OpenAIResponsesClient({}, {}, {}, parent)
{}

OpenAIResponsesClient::OpenAIResponsesClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : BaseClient(url, apiKey, model, parent)
{}

QNetworkRequest OpenAIResponsesClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString key = m_apiKey;
    if (!key.isEmpty())
        request.setRawHeader("Authorization", QString("Bearer %1").arg(key).toUtf8());

    return request;
}

RequestID OpenAIResponsesClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

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

QFuture<QList<QString>> OpenAIResponsesClient::listModels()
{
    QUrl url(m_url + "/models");
    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->send(request, QByteArrayView("GET"))
        .then(this, [](const HttpResponse &response) {
            QList<QString> models;
            if (!response.isSuccess()) {
                qCDebug(llmOpenAILog).noquote()
                    << QString("Error fetching models: HTTP %1").arg(response.statusCode);
                return models;
            }

            QJsonObject json = QJsonDocument::fromJson(response.body).object();
            if (json.contains("data")) {
                QJsonArray modelArray = json["data"].toArray();
                for (const QJsonValue &value : modelArray) {
                    QJsonObject modelObject = value.toObject();
                    if (modelObject.contains("id"))
                        models.append(modelObject["id"].toString());
                }
            }
            return models;
        })
        .onFailed(this, [](const std::exception &e) {
            qCDebug(llmOpenAILog).noquote() << QString("Error fetching models: %1").arg(e.what());
            return QList<QString>{};
        });
}

QString OpenAIResponsesClient::parseHttpError(const HttpResponse &response) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(response.body);
    if (doc.isObject()) {
        const QJsonObject error = doc.object().value("error").toObject();
        const QString message = error.value("message").toString();
        const QString type = error.value("type").toString();
        const QString code = error.value("code").toString();
        if (!message.isEmpty()) {
            QString out = QString("HTTP %1: %2").arg(response.statusCode).arg(message);
            if (!type.isEmpty())
                out += QString(" (type: %1)").arg(type);
            if (!code.isEmpty())
                out += QString(" (code: %1)").arg(code);
            return out;
        }
    }
    return BaseClient::parseHttpError(response);
}

void OpenAIResponsesClient::processData(const RequestID &id, const QByteArray &data)
{
    if (!hasRequest(id))
        return;

    const QList<SSEEvent> events = requestSSEParser(id).append(data);
    for (const SSEEvent &ev : events) {
        if (ev.data.isEmpty() || ev.data == "[DONE]")
            continue;
        const QJsonObject payload = QJsonDocument::fromJson(ev.data).object();
        if (payload.isEmpty())
            continue;
        processStreamEvent(id, ev.type, payload);
    }
}

BaseMessage *OpenAIResponsesClient::messageForRequest(const RequestID &id) const
{
    return m_messages.value(id, nullptr);
}

void OpenAIResponsesClient::cleanupDerivedData(const RequestID &id)
{
    if (auto *msg = m_messages.take(id))
        msg->deleteLater();

    m_itemIdToCallId.remove(id);
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

    QList<QJsonObject> assistantItems = responsesMsg->toItemsFormat();
    for (const QJsonObject &item : assistantItems)
        input.append(item);

    QJsonArray toolResultItems = responsesMsg->createToolResultItems(toolResults);
    for (const QJsonValue &item : toolResultItems)
        input.append(item);

    request["input"] = input;
    return request;
}

void OpenAIResponsesClient::processStreamEvent(
    const RequestID &id, const QString &eventType, const QJsonObject &data)
{
    OpenAIResponsesMessage *message = m_messages.value(id);
    if (!message) {
        message = new OpenAIResponsesMessage(this);
        m_messages[id] = message;
    } else if (message->state() == MessageState::RequiresToolExecution) {
        message->startNewContinuation();
    }

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
            if (!itemId.isEmpty())
                message->handleReasoningStart(itemId);
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
                message->handleReasoningComplete(finalItemId);
                notifyPendingThinkingBlocks(id);
            }
        } else if (item.isEmpty() && !itemId.isEmpty()) {
            QString callId = m_itemIdToCallId.value(id).value(itemId);
            if (!callId.isEmpty())
                message->handleToolCallComplete(callId);
        } else if (!item.isEmpty() && item["type"].toString() == "function_call") {
            QString callId = item["call_id"].toString();
            if (!callId.isEmpty())
                message->handleToolCallComplete(callId);
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

void OpenAIResponsesClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        failRequest(id, QStringLiteral("Invalid JSON in buffered response"));
        return;
    }

    QJsonObject response = doc.object();

    if (response["error"].isObject()) {
        QJsonObject error = response["error"].toObject();
        failRequest(id, error["message"].toString());
        return;
    }

    auto *message = new OpenAIResponsesMessage(this);
    m_messages[id] = message;

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
}

} // namespace LLMQore
