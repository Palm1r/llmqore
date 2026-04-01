// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/LlamaCppClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "clients/openai/OpenAIMessage.hpp"
#include <LLMCore/HttpClient.hpp>
#include <LLMCore/Log.hpp>
#include <LLMCore/SSEBuffer.hpp>
#include <LLMCore/SSEUtils.hpp>

namespace LLMCore {

LlamaCppClient::LlamaCppClient(QObject *parent)
    : LlamaCppClient({}, {}, {}, parent)
{}

LlamaCppClient::LlamaCppClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : BaseClient(url, apiKey, model, parent)
{}

QNetworkRequest LlamaCppClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString key = m_apiKey;
    if (!key.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + key).toUtf8());

    return request;
}

bool LlamaCppClient::isInfillRequest(const QJsonObject &payload)
{
    return payload.contains("input_prefix") || payload.contains("input_suffix");
}

RequestID LlamaCppClient::sendMessage(
    const QJsonObject &payload, RequestCallbacks callbacks, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    RequestID id = createRequest(std::move(callbacks));

    QString endpoint = isInfillRequest(payload) ? "/infill" : "/v1/chat/completions";

    qCDebug(llmLlamaCppLog).noquote() << QString("Sending request %1 to %2").arg(id, endpoint);

    sendRequest(id, QUrl(m_url + endpoint), request, mode);
    return id;
}

RequestID LlamaCppClient::ask(const QString &prompt, RequestCallbacks callbacks, RequestMode mode)
{
    QJsonObject payload;
    if (!m_model.isEmpty())
        payload["model"] = m_model;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}};

    return sendMessage(payload, std::move(callbacks), mode);
}

QFuture<QList<QString>> LlamaCppClient::listModels()
{
    QUrl url(m_url + "/v1/models");
    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->get(request)
        .then([](const QByteArray &data) {
            QList<QString> models;
            QJsonObject json = QJsonDocument::fromJson(data).object();

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
        .onFailed([](const std::exception &e) {
            qCDebug(llmLlamaCppLog).noquote() << QString("Error fetching models: %1").arg(e.what());
            return QList<QString>{};
        });
}

QFuture<bool> LlamaCppClient::isServerReady()
{
    QUrl url(m_url + "/health");
    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->get(request)
        .then([](const QByteArray &data) {
            QJsonObject json = QJsonDocument::fromJson(data).object();
            return json["status"].toString() == "ok";
        })
        .onFailed([](const std::exception &) { return false; });
}

QFuture<QJsonObject> LlamaCppClient::serverProps()
{
    QUrl url(m_url + "/props");
    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->get(request)
        .then([](const QByteArray &data) { return QJsonDocument::fromJson(data).object(); })
        .onFailed([](const std::exception &) { return QJsonObject{}; });
}

void LlamaCppClient::processData(const RequestID &id, const QByteArray &data)
{
    if (!hasRequest(id))
        return;

    QStringList lines = requestSSEBuffer(id).processData(data);

    for (const QString &line : lines) {
        if (line.trimmed().isEmpty() || line == "data: [DONE]")
            continue;

        QJsonObject chunk = SSEUtils::parseEventLine(line);
        if (chunk.isEmpty())
            continue;

        if (chunk.contains("content") && !chunk.contains("choices")) {
            QString content = chunk["content"].toString();
            if (!content.isEmpty())
                addChunk(id, content);

            if (chunk["stop"].toBool()) {
                cleanupFullRequest(id);
                completeRequest(id);
                return;
            }
        } else if (chunk.contains("choices")) {
            processStreamChunk(id, chunk);
        }
    }
}

BaseMessage *LlamaCppClient::messageForRequest(const RequestID &id) const
{
    return m_messages.value(id, nullptr);
}

void LlamaCppClient::cleanupDerivedData(const RequestID &id)
{
    if (auto *msg = m_messages.take(id))
        msg->deleteLater();

    m_reasoningContent.remove(id);
    m_thinkingEmitted.remove(id);
}

QJsonObject LlamaCppClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, QString> &toolResults)
{
    auto *openaiMsg = qobject_cast<OpenAIMessage *>(message);
    if (!openaiMsg)
        return originalPayload;

    QJsonObject request = originalPayload;
    QJsonArray messages = request["messages"].toArray();

    messages.append(openaiMsg->toProviderFormat());

    QJsonArray toolResultMessages = openaiMsg->createToolResultMessages(toolResults);
    for (const auto &toolMsg : toolResultMessages)
        messages.append(toolMsg);

    request["messages"] = messages;
    return request;
}

void LlamaCppClient::onStreamFinished(const RequestID &id, std::optional<QString> error)
{
    if (!error && hasRequest(id)) {
        SSEBuffer &buffer = requestSSEBuffer(id);
        if (buffer.hasIncompleteData()) {
            QString remaining = buffer.currentBuffer().trimmed();
            buffer.clear();

            if (!remaining.isEmpty()) {
                QJsonObject chunk = SSEUtils::parseEventLine(remaining);
                if (!chunk.isEmpty()) {
                    if (chunk.contains("content") && !chunk.contains("choices")) {
                        QString content = chunk["content"].toString();
                        if (!content.isEmpty())
                            addChunk(id, content);
                    } else if (chunk.contains("choices")) {
                        processStreamChunk(id, chunk);
                    }
                }
            }
        }
    }

    BaseClient::onStreamFinished(id, error);
}

void LlamaCppClient::emitPendingThinking(const RequestID &id)
{
    if (m_thinkingEmitted.contains(id))
        return;

    QString thinking = m_reasoningContent.value(id);
    if (thinking.trimmed().isEmpty())
        return;

    notifyThinkingBlock(id, thinking, QString());
    m_thinkingEmitted.insert(id);
}

void LlamaCppClient::processStreamChunk(const RequestID &id, const QJsonObject &chunk)
{
    QJsonArray choices = chunk["choices"].toArray();
    if (choices.isEmpty())
        return;

    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice["delta"].toObject();
    QString finishReason = choice["finish_reason"].toString();

    OpenAIMessage *message = m_messages.value(id);
    if (!message) {
        message = new OpenAIMessage(this);
        m_messages[id] = message;
        qCDebug(llmLlamaCppLog).noquote()
            << QString("Created OpenAIMessage for request %1").arg(id);
    } else if (message->state() == MessageState::RequiresToolExecution) {
        message->startNewContinuation();
        m_thinkingEmitted.remove(id);
        qCDebug(llmLlamaCppLog).noquote()
            << QString("Starting continuation for request %1").arg(id);
    }

    if (delta.contains("reasoning_content") && !delta["reasoning_content"].isNull()) {
        QString reasoning = delta["reasoning_content"].toString();
        if (!reasoning.isEmpty())
            m_reasoningContent[id] += reasoning;
    }

    if (delta.contains("content") && !delta["content"].isNull()) {
        QString content = delta["content"].toString();
        if (!content.isEmpty()) {
            emitPendingThinking(id);
            message->handleContentDelta(content);
            addChunk(id, content);
        }
    }

    if (delta.contains("tool_calls")) {
        QJsonArray toolCalls = delta["tool_calls"].toArray();
        for (const auto &toolCallValue : toolCalls) {
            QJsonObject toolCall = toolCallValue.toObject();
            int index = toolCall["index"].toInt();

            if (toolCall.contains("id")) {
                QString toolId = toolCall["id"].toString();
                QJsonObject function = toolCall["function"].toObject();
                QString name = function["name"].toString();
                message->handleToolCallStart(index, toolId, name);
            }

            if (toolCall.contains("function")) {
                QJsonObject function = toolCall["function"].toObject();
                if (function.contains("arguments"))
                    message->handleToolCallDelta(index, function["arguments"].toString());
            }
        }
    }

    if (!finishReason.isEmpty() && finishReason != "null") {
        emitPendingThinking(id);
        message->completeAllPendingToolCalls();
        message->handleFinishReason(finishReason);
        executeToolsFromMessage(id);
    }
}

void LlamaCppClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
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

    if (response.contains("content") && !response.contains("choices")) {
        QString content = response["content"].toString();
        if (!content.isEmpty())
            addChunk(id, content);

        cleanupFullRequest(id);
        completeRequest(id);
        return;
    }

    QJsonArray choices = response["choices"].toArray();
    if (choices.isEmpty()) {
        failRequest(id, QStringLiteral("Empty choices in buffered response"));
        return;
    }

    QJsonObject choice = choices[0].toObject();
    QJsonObject messageObj = choice["message"].toObject();
    QString finishReason = choice["finish_reason"].toString();

    auto *message = new OpenAIMessage(this);
    m_messages[id] = message;

    if (messageObj.contains("reasoning_content") && !messageObj["reasoning_content"].isNull()) {
        QString reasoning = messageObj["reasoning_content"].toString();
        if (!reasoning.trimmed().isEmpty())
            notifyThinkingBlock(id, reasoning, QString());
    }

    QString content = messageObj["content"].toString();
    if (!content.isEmpty()) {
        message->handleContentDelta(content);
        addChunk(id, content);
    }

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
}

} // namespace LLMCore
