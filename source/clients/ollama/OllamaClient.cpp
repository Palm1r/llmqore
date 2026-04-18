// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OllamaClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "OllamaMessage.hpp"
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/LineBuffer.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

OllamaClient::OllamaClient(QObject *parent)
    : OllamaClient({}, {}, {}, parent)
{}

OllamaClient::OllamaClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : BaseClient(url, apiKey, model, parent)
{}

QNetworkRequest OllamaClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString key = m_apiKey;
    if (!key.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + key).toUtf8());
    return request;
}

RequestID OllamaClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/api/chat") : endpoint;

    qCDebug(llmOllamaLog).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

RequestID OllamaClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["model"] = m_model;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}};

    return sendMessage(payload, {}, mode);
}

QFuture<QList<QString>> OllamaClient::listModels()
{
    QUrl url(m_url + "/api/tags");
    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->send(request, QByteArrayView("GET"))
        .then(this, [](const HttpResponse &response) {
            QList<QString> models;
            if (!response.isSuccess()) {
                qCDebug(llmOllamaLog).noquote()
                    << QString("Error fetching models: HTTP %1").arg(response.statusCode);
                return models;
            }

            QJsonObject json = QJsonDocument::fromJson(response.body).object();
            QJsonArray modelArray = json["models"].toArray();
            for (const QJsonValue &value : modelArray) {
                QJsonObject modelObject = value.toObject();
                models.append(modelObject["name"].toString());
            }
            return models;
        })
        .onFailed(this, [](const std::exception &e) {
            qCDebug(llmOllamaLog).noquote() << QString("Error fetching models: %1").arg(e.what());
            return QList<QString>{};
        });
}

QString OllamaClient::parseHttpError(const HttpResponse &response) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(response.body);
    if (doc.isObject()) {
        const QString message = doc.object().value("error").toString();
        if (!message.isEmpty())
            return QString("HTTP %1: %2").arg(response.statusCode).arg(message);
    }
    return BaseClient::parseHttpError(response);
}

void OllamaClient::processData(const RequestID &id, const QByteArray &data)
{
    if (data.isEmpty())
        return;

    if (!hasRequest(id))
        return;

    QStringList lines = requestLineBuffer(id).processData(data);

    for (const QString &line : lines) {
        if (line.trimmed().isEmpty())
            continue;

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);
        if (doc.isNull()) {
            qCDebug(llmOllamaLog).noquote()
                << QString("Failed to parse JSON: %1").arg(error.errorString());
            continue;
        }

        QJsonObject obj = doc.object();

        if (obj.contains("error") && !obj["error"].toString().isEmpty()) {
            QString errorMsg = obj["error"].toString();
            qCWarning(llmOllamaLog).noquote() << "Error in response: " + errorMsg;
            cleanupFullRequest(id);
            failRequest(id, errorMsg);
            return;
        }

        processStreamData(id, obj);
    }
}

BaseMessage *OllamaClient::messageForRequest(const RequestID &id) const
{
    return m_messages.value(id, nullptr);
}

void OllamaClient::cleanupDerivedData(const RequestID &id)
{
    if (auto *msg = m_messages.take(id))
        msg->deleteLater();

    m_thinkingEmitted.remove(id);
}

QJsonObject OllamaClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    auto *ollamaMsg = qobject_cast<OllamaMessage *>(message);
    if (!ollamaMsg)
        return originalPayload;

    QJsonObject request = originalPayload;
    QJsonArray messages = request["messages"].toArray();

    messages.append(ollamaMsg->toProviderFormat());

    QJsonArray toolResultMessages = ollamaMsg->createToolResultMessages(toolResults);
    for (const auto &toolMsg : toolResultMessages)
        messages.append(toolMsg);

    request["messages"] = messages;
    return request;
}

void OllamaClient::onStreamFinished(const RequestID &id, std::optional<QString> error)
{
    if (!error && hasRequest(id)) {
        LineBuffer &buffer = requestLineBuffer(id);
        if (buffer.hasIncompleteData()) {
            QString remaining = buffer.currentBuffer().trimmed();
            buffer.clear();

            if (!remaining.isEmpty()) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(remaining.toUtf8(), &parseError);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("error") && !obj["error"].toString().isEmpty()) {
                        QString errorMsg = obj["error"].toString();
                        qCWarning(llmOllamaLog).noquote()
                            << "Error in remaining buffer: " + errorMsg;
                        cleanupFullRequest(id);
                        failRequest(id, errorMsg);
                        return;
                    }
                    processStreamData(id, obj);
                }
            }
        }
    }

    BaseClient::onStreamFinished(id, error);
}

void OllamaClient::processStreamData(const RequestID &id, const QJsonObject &data)
{
    OllamaMessage *message = m_messages.value(id);
    if (!message) {
        message = new OllamaMessage(this);
        m_messages[id] = message;
        qCDebug(llmOllamaLog).noquote() << QString("Created OllamaMessage for request %1").arg(id);
    } else if (message->state() == MessageState::RequiresToolExecution) {
        message->startNewContinuation();
        m_thinkingEmitted.remove(id);
        qCDebug(llmOllamaLog).noquote() << QString("Starting continuation for request %1").arg(id);
    }

    if (data.contains("thinking")) {
        QString thinkingDelta = data["thinking"].toString();
        if (!thinkingDelta.isEmpty())
            message->handleThinkingDelta(thinkingDelta);
    }

    if (data.contains("message")) {
        QJsonObject messageObj = data["message"].toObject();

        if (messageObj.contains("thinking")) {
            QString thinkingDelta = messageObj["thinking"].toString();
            if (!thinkingDelta.isEmpty())
                message->handleThinkingDelta(thinkingDelta);
        }

        if (messageObj.contains("content")) {
            QString content = messageObj["content"].toString();
            if (!content.isEmpty()) {
                notifyThinkingBlocks(id, message);
                message->handleContentDelta(content);
                addChunk(id, content);
            }
        }

        if (messageObj.contains("tool_calls")) {
            QJsonArray toolCalls = messageObj["tool_calls"].toArray();
            for (const auto &toolCallValue : toolCalls)
                message->handleToolCall(toolCallValue.toObject());
        }
    } else if (data.contains("response")) {
        QString content = data["response"].toString();
        if (!content.isEmpty()) {
            message->handleContentDelta(content);
            addChunk(id, content);
        }
    }

    if (data["done"].toBool()) {
        if (data.contains("signature")) {
            message->handleThinkingComplete(data["signature"].toString());
        }

        message->handleDone(true);

        notifyThinkingBlocks(id, message);
        executeToolsFromMessage(id);
    }
}

void OllamaClient::notifyThinkingBlocks(const RequestID &id, OllamaMessage *message)
{
    if (!message || m_thinkingEmitted.contains(id))
        return;

    auto thinkingBlocks = message->getCurrentThinkingContent();
    if (thinkingBlocks.isEmpty())
        return;

    for (auto *thinkingContent : thinkingBlocks) {
        if (!thinkingContent->thinking().trimmed().isEmpty()) {
            emit thinkingBlockReceived(
                id, thinkingContent->thinking(), thinkingContent->signature());
        }
    }

    m_thinkingEmitted.insert(id);
}

void OllamaClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        failRequest(id, QStringLiteral("Invalid JSON in buffered response"));
        return;
    }

    QJsonObject response = doc.object();

    if (response.contains("error") && !response["error"].toString().isEmpty()) {
        failRequest(id, response["error"].toString());
        return;
    }

    processStreamData(id, response);
}

} // namespace LLMQore
