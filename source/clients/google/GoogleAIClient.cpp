// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/GoogleAIClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>

#include "GoogleMessage.hpp"
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

namespace LLMQore {

GoogleAIClient::GoogleAIClient(QObject *parent)
    : GoogleAIClient({}, {}, {}, parent)
{}

GoogleAIClient::GoogleAIClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : BaseClient(url, apiKey, model, parent)
{}

QNetworkRequest GoogleAIClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString key = m_apiKey;
    if (!key.isEmpty()) {
        QUrl requestUrl = request.url();
        QUrlQuery query(requestUrl.query());
        query.addQueryItem("key", key);
        requestUrl.setQuery(query);
        request.setUrl(requestUrl);
    }

    return request;
}

RequestID GoogleAIClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    RequestID id = createRequest();

    QString resolved = endpoint;
    if (resolved.isEmpty()) {
        const QString modelName = payload.contains("model") ? payload["model"].toString() : m_model;
        const QString suffix = (mode == RequestMode::Streaming)
                                   ? QStringLiteral(":streamGenerateContent?alt=sse")
                                   : QStringLiteral(":generateContent");
        resolved = QStringLiteral("/models/%1%2").arg(modelName, suffix);
    }
    QUrl url(m_url + resolved);

    qCDebug(llmGoogleLog).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, url, payload, mode);
    return id;
}

RequestID GoogleAIClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["contents"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"parts", QJsonArray{QJsonObject{{"text", prompt}}}}}};

    return sendMessage(payload, {}, mode);
}

QFuture<QList<QString>> GoogleAIClient::listModels()
{
    QUrl url(QString("%1/models").arg(m_url));
    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->send(request, QByteArrayView("GET"))
        .then(this, [](const HttpResponse &response) {
            QList<QString> models;
            if (!response.isSuccess()) {
                qCDebug(llmGoogleLog).noquote()
                    << QString("Error fetching models: HTTP %1").arg(response.statusCode);
                return models;
            }

            QJsonObject json = QJsonDocument::fromJson(response.body).object();
            if (json.contains("models")) {
                QJsonArray modelArray = json["models"].toArray();
                for (const QJsonValue &value : modelArray) {
                    QJsonObject modelObject = value.toObject();
                    if (modelObject.contains("name")) {
                        QString modelName = modelObject["name"].toString();
                        if (modelName.contains("/"))
                            modelName = modelName.split("/").last();
                        models.append(modelName);
                    }
                }
            }
            return models;
        })
        .onFailed(this, [](const std::exception &e) {
            qCDebug(llmGoogleLog).noquote() << QString("Error fetching models: %1").arg(e.what());
            return QList<QString>{};
        });
}

QString GoogleAIClient::parseHttpError(const HttpResponse &response) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(response.body);
    if (doc.isObject()) {
        const QJsonObject error = doc.object().value("error").toObject();
        const QString message = error.value("message").toString();
        const int code = error.value("code").toInt();
        const QString status = error.value("status").toString();
        if (!message.isEmpty()) {
            QString out = QString("HTTP %1: %2").arg(response.statusCode).arg(message);
            if (code != 0)
                out += QString(" (code: %1)").arg(code);
            if (!status.isEmpty())
                out += QString(" (status: %1)").arg(status);
            return out;
        }
    }
    return BaseClient::parseHttpError(response);
}

void GoogleAIClient::processData(const RequestID &id, const QByteArray &data)
{
    if (data.isEmpty())
        return;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isNull() && doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("error")) {
            QJsonObject error = obj["error"].toObject();
            QString errorMessage = error["message"].toString();
            int errorCode = error["code"].toInt();
            QString fullError
                = QString("Google AI API Error %1: %2").arg(errorCode).arg(errorMessage);

            qCDebug(llmGoogleLog).noquote() << fullError;
            m_failedRequests.insert(id, fullError);
            return;
        }
    }

    if (!hasRequest(id))
        return;

    const QList<SSEEvent> events = requestSSEParser(id).append(data);
    for (const SSEEvent &ev : events) {
        if (ev.data.isEmpty() || ev.data == "[DONE]")
            continue;
        const QJsonObject chunk = QJsonDocument::fromJson(ev.data).object();
        if (chunk.isEmpty())
            continue;
        processStreamChunk(id, chunk);
    }
}

void GoogleAIClient::onStreamFinished(const RequestID &id, std::optional<QString> error)
{
    if (error) {
        cleanupFullRequest(id);
        failRequest(id, *error);
        return;
    }

    if (m_failedRequests.contains(id)) {
        QString failError = m_failedRequests.take(id);
        cleanupFullRequest(id);
        failRequest(id, failError);
        return;
    }

    notifyPendingThinkingBlocks(id);

    if (m_messages.contains(id)) {
        GoogleMessage *message = m_messages[id];
        executeToolsFromMessage(id);

        if (message->state() == MessageState::RequiresToolExecution) {
            qCDebug(llmGoogleLog).noquote()
                << QString("Waiting for tools to complete for %1").arg(id);
            return;
        }
    }

    cleanupFullRequest(id);
    completeRequest(id);
}

void GoogleAIClient::processStreamChunk(const RequestID &id, const QJsonObject &chunk)
{
    if (!chunk.contains("candidates"))
        return;

    GoogleMessage *message = m_messages.value(id);
    if (!message) {
        message = new GoogleMessage(this);
        m_messages[id] = message;
        qCDebug(llmGoogleLog).noquote() << QString("Created GoogleMessage for request %1").arg(id);
    } else if (message->state() == MessageState::RequiresToolExecution) {
        message->startNewContinuation();
        qCDebug(llmGoogleLog).noquote() << QString("Starting continuation for request %1").arg(id);
    }

    QJsonArray candidates = chunk["candidates"].toArray();
    for (const QJsonValue &candidate : candidates) {
        QJsonObject candidateObj = candidate.toObject();

        if (candidateObj.contains("content")) {
            QJsonObject content = candidateObj["content"].toObject();
            if (content.contains("parts")) {
                QJsonArray parts = content["parts"].toArray();
                for (const QJsonValue &part : parts) {
                    QJsonObject partObj = part.toObject();

                    if (partObj.contains("text")) {
                        QString text = partObj["text"].toString();
                        bool isThought = partObj.value("thought").toBool(false);

                        if (isThought) {
                            message->handleThoughtDelta(text);

                            if (partObj.contains("signature"))
                                message->handleThoughtSignature(partObj["signature"].toString());
                        } else {
                            notifyPendingThinkingBlocks(id);
                            message->handleContentDelta(text);
                            addChunk(id, text);
                        }
                    }

                    if (partObj.contains("thoughtSignature"))
                        message->handleThoughtSignature(partObj["thoughtSignature"].toString());

                    if (partObj.contains("functionCall")) {
                        notifyPendingThinkingBlocks(id);

                        QJsonObject functionCall = partObj["functionCall"].toObject();
                        QString name = functionCall["name"].toString();
                        QJsonObject args = functionCall["args"].toObject();

                        message->handleFunctionCallStart(name);
                        message->handleFunctionCallArgsDelta(
                            QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
                        message->handleFunctionCallComplete();
                    }
                }
            }
        }

        if (candidateObj.contains("finishReason")) {
            QString finishReason = candidateObj["finishReason"].toString();
            message->handleFinishReason(finishReason);

            if (message->isErrorFinishReason()) {
                QString errorMessage = message->getErrorMessage();
                qCDebug(llmGoogleLog).noquote() << QString("Google AI error: %1").arg(errorMessage);
                m_failedRequests.insert(id, errorMessage);
                cleanupFullRequest(id);
                failRequest(id, errorMessage);
                return;
            }
        }
    }
}

BaseMessage *GoogleAIClient::messageForRequest(const RequestID &id) const
{
    return m_messages.value(id, nullptr);
}

void GoogleAIClient::cleanupDerivedData(const RequestID &id)
{
    if (auto *msg = m_messages.take(id))
        msg->deleteLater();

    m_failedRequests.remove(id);
}

QJsonObject GoogleAIClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    auto *googleMsg = qobject_cast<GoogleMessage *>(message);
    if (!googleMsg)
        return originalPayload;

    QJsonObject request = originalPayload;
    QJsonArray contents = request["contents"].toArray();

    contents.append(googleMsg->toProviderFormat());

    QJsonObject functionMessage;
    functionMessage["role"] = "function";
    functionMessage["parts"] = googleMsg->createToolResultParts(toolResults);
    contents.append(functionMessage);

    request["contents"] = contents;
    return request;
}

void GoogleAIClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        failRequest(id, QStringLiteral("Invalid JSON in buffered response"));
        return;
    }

    QJsonObject response = doc.object();

    if (response["error"].isObject()) {
        QJsonObject error = response["error"].toObject();
        QString errorMessage = error["message"].toString();
        int errorCode = error["code"].toInt();
        failRequest(id, QString("Google AI API Error %1: %2").arg(errorCode).arg(errorMessage));
        return;
    }

    processStreamChunk(id, response);
}

} // namespace LLMQore
