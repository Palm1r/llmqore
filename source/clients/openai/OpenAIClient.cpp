// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OpenAIClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "OpenAIMessage.hpp"
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

OpenAIClient::OpenAIClient(QObject *parent)
    : OpenAIClient({}, {}, {}, parent)
{}

OpenAIClient::OpenAIClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : BaseClient(url, apiKey, model, parent)
{}

QNetworkRequest OpenAIClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString key = m_apiKey;
    if (!key.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + key).toUtf8());

    return request;
}

RequestID OpenAIClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/chat/completions") : endpoint;

    qCDebug(llmOpenAILog).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

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

QFuture<QList<QString>> OpenAIClient::listModels()
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

QString OpenAIClient::parseHttpError(const HttpResponse &response) const
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

void OpenAIClient::processData(const RequestID &id, const QByteArray &data)
{
    if (!hasRequest(id))
        return;

    const QList<SSEEvent> events = requestSSEParser(id).append(data);
    for (const SSEEvent &ev : events) {
        if (ev.data.isEmpty() || ev.data == "[DONE]")
            continue;
        const QJsonObject chunk = QJsonDocument::fromJson(ev.data).object();
        if (chunk.isEmpty())
            continue;
        if (chunk.contains("choices"))
            processStreamChunk(id, chunk);
    }
}

BaseMessage *OpenAIClient::messageForRequest(const RequestID &id) const
{
    return m_messages.value(id, nullptr);
}

void OpenAIClient::cleanupDerivedData(const RequestID &id)
{
    if (auto *msg = m_messages.take(id))
        msg->deleteLater();
}

QJsonObject OpenAIClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
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

void OpenAIClient::processStreamChunk(const RequestID &id, const QJsonObject &chunk)
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
        qCDebug(llmOpenAILog).noquote() << QString("Created OpenAIMessage for request %1").arg(id);
    } else if (message->state() == MessageState::RequiresToolExecution) {
        message->startNewContinuation();
        qCDebug(llmOpenAILog).noquote() << QString("Starting continuation for request %1").arg(id);
    }

    if (delta.contains("content") && !delta["content"].isNull()) {
        QString content = delta["content"].toString();
        message->handleContentDelta(content);
        addChunk(id, content);
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
        message->completeAllPendingToolCalls();
        message->handleFinishReason(finishReason);
        executeToolsFromMessage(id);
    }
}

void OpenAIClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
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

} // namespace LLMQore
