// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/ClaudeClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>

#include "ClaudeMessage.hpp"
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

ClaudeClient::ClaudeClient(QObject *parent)
    : ClaudeClient({}, {}, {}, parent)
{}

ClaudeClient::ClaudeClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : BaseClient(url, apiKey, model, parent)
{}

QNetworkRequest ClaudeClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("anthropic-version", "2023-06-01");

    QString key = m_apiKey;
    if (!key.isEmpty())
        request.setRawHeader("x-api-key", key.toUtf8());

    return request;
}

RequestID ClaudeClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/v1/messages") : endpoint;

    qCDebug(llmClaudeLog).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

RequestID ClaudeClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["model"] = m_model;
    payload["max_tokens"] = 4096;
    payload["messages"] = QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}};

    return sendMessage(payload, {}, mode);
}

QFuture<QList<QString>> ClaudeClient::listModels()
{
    QUrl url(m_url + "/v1/models");
    QUrlQuery query;
    query.addQueryItem("limit", "1000");
    url.setQuery(query);

    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->send(request, QByteArrayView("GET"))
        .then(this, [](const HttpResponse &response) {
            QList<QString> models;
            if (!response.isSuccess()) {
                qCDebug(llmClaudeLog).noquote()
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
            qCDebug(llmClaudeLog).noquote() << QString("Error fetching models: %1").arg(e.what());
            return QList<QString>{};
        });
}

QString ClaudeClient::parseHttpError(const HttpResponse &response) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(response.body);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonObject error = root.value("error").toObject();
        const QString message = error.value("message").toString();
        const QString type = error.value("type").toString();
        if (!message.isEmpty()) {
            if (!type.isEmpty())
                return QString("HTTP %1: %2 (%3)")
                    .arg(response.statusCode)
                    .arg(message)
                    .arg(type);
            return QString("HTTP %1: %2").arg(response.statusCode).arg(message);
        }
    }
    return BaseClient::parseHttpError(response);
}

void ClaudeClient::processData(const RequestID &id, const QByteArray &data)
{
    if (!hasRequest(id))
        return;

    const QList<SSEEvent> events = requestSSEParser(id).append(data);
    for (const SSEEvent &ev : events) {
        if (ev.data.isEmpty() || ev.data == "[DONE]")
            continue;
        const QJsonObject json = QJsonDocument::fromJson(ev.data).object();
        if (!json.isEmpty())
            processStreamEvent(id, json);
    }
}

BaseMessage *ClaudeClient::messageForRequest(const RequestID &id) const
{
    return m_messages.value(id, nullptr);
}

void ClaudeClient::cleanupDerivedData(const RequestID &id)
{
    if (auto *msg = m_messages.take(id))
        msg->deleteLater();
}

QJsonObject ClaudeClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    auto *claudeMsg = qobject_cast<ClaudeMessage *>(message);
    if (!claudeMsg)
        return originalPayload;

    QJsonObject request = originalPayload;
    QJsonArray messages = request["messages"].toArray();

    messages.append(claudeMsg->toProviderFormat());

    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = claudeMsg->createToolResultsContent(toolResults);
    messages.append(userMessage);

    request["messages"] = messages;
    return request;
}

void ClaudeClient::processStreamEvent(const RequestID &id, const QJsonObject &event)
{
    QString eventType = event["type"].toString();

    if (eventType == "message_stop")
        return;

    ClaudeMessage *message = m_messages.value(id);
    if (!message) {
        if (eventType == "message_start") {
            message = new ClaudeMessage(this);
            m_messages[id] = message;
            qCDebug(llmClaudeLog).noquote()
                << QString("Created ClaudeMessage for request %1").arg(id);
        } else {
            qCWarning(llmClaudeLog).noquote()
                << QString("Dropping event '%1' for request %2: no active message (missing "
                           "message_start?)")
                       .arg(eventType, id);
            return;
        }
    }

    if (eventType == "message_start") {
        message->startNewContinuation();
        qCDebug(llmClaudeLog).noquote() << QString("Starting continuation for request %1").arg(id);

    } else if (eventType == "content_block_start") {
        int index = event["index"].toInt();
        QJsonObject contentBlock = event["content_block"].toObject();
        QString blockType = contentBlock["type"].toString();

        message->handleContentBlockStart(index, blockType, contentBlock);

    } else if (eventType == "content_block_delta") {
        int index = event["index"].toInt();
        QJsonObject delta = event["delta"].toObject();
        QString deltaType = delta["type"].toString();

        message->handleContentBlockDelta(index, deltaType, delta);

        if (deltaType == "text_delta") {
            QString text = delta["text"].toString();
            addChunk(id, text);
        }

    } else if (eventType == "content_block_stop") {
        int index = event["index"].toInt();

        if (auto *tc = dynamic_cast<ThinkingContent *>(message->blockAt(index))) {
            emit thinkingBlockReceived(id, tc->thinking(), tc->signature());
        } else if (auto *rc = dynamic_cast<RedactedThinkingContent *>(message->blockAt(index))) {
            emit thinkingBlockReceived(id, QString(), rc->signature());
        }

        message->handleContentBlockStop(index);

    } else if (eventType == "message_delta") {
        QJsonObject delta = event["delta"].toObject();
        if (delta.contains("stop_reason")) {
            message->handleStopReason(delta["stop_reason"].toString());
            executeToolsFromMessage(id);
        }
    }
}

void ClaudeClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
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

    auto *message = new ClaudeMessage(this);
    m_messages[id] = message;
    message->startNewContinuation();

    QJsonArray content = response["content"].toArray();
    for (int i = 0; i < content.size(); ++i) {
        QJsonObject block = content[i].toObject();
        QString blockType = block["type"].toString();

        message->handleContentBlockStart(i, blockType, block);

        if (blockType == "text") {
            QString text = block["text"].toString();
            if (!text.isEmpty()) {
                message->handleContentBlockDelta(
                    i, QStringLiteral("text_delta"), QJsonObject{{"text", text}});
                addChunk(id, text);
            }
        } else if (blockType == "thinking") {
            QString thinking = block["thinking"].toString();
            if (!thinking.isEmpty()) {
                message->handleContentBlockDelta(
                    i, QStringLiteral("thinking_delta"), QJsonObject{{"thinking", thinking}});
            }
            if (block.contains("signature")) {
                message->handleContentBlockDelta(
                    i,
                    QStringLiteral("signature_delta"),
                    QJsonObject{{"signature", block["signature"].toString()}});
            }
            if (auto *tc = dynamic_cast<ThinkingContent *>(message->blockAt(i)))
                emit thinkingBlockReceived(id, tc->thinking(), tc->signature());
        } else if (blockType == "redacted_thinking") {
            if (auto *rc = dynamic_cast<RedactedThinkingContent *>(message->blockAt(i)))
                emit thinkingBlockReceived(id, QString(), rc->signature());
        }

        message->handleContentBlockStop(i);
    }

    QString stopReason = response["stop_reason"].toString();
    if (!stopReason.isEmpty()) {
        message->handleStopReason(stopReason);
        executeToolsFromMessage(id);
    }
}

} // namespace LLMQore
