// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/GoogleAIClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "GoogleMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

#include "core/ThreadAffinity.hpp"

namespace LLMQore {

namespace {

const UsageSchema kGoogleUsage{
    QLatin1String("usageMetadata"),
    {{}, QLatin1String("promptTokenCount")},
    {{}, QLatin1String("candidatesTokenCount")},
    {{}, QLatin1String("cachedContentTokenCount")},
    {{}, QLatin1String("thoughtsTokenCount")}};

} // namespace

GoogleAIClient::GoogleAIClient(QObject *parent)
    : GoogleAIClient({}, {}, {}, parent)
{}

GoogleAIClient::GoogleAIClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : GoogleAIClient(url, apiKey, model, nullptr, parent)
{}

GoogleAIClient::GoogleAIClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : BaseClient(url, apiKey, model, transport, parent)
{
    setLogCategory(llmGoogleLog());
    setAuthScheme(
        {.placement = AuthScheme::Placement::QueryParam, .name = QStringLiteral("key")});
    setHeaders({{QStringLiteral("Content-Type"), QStringLiteral("application/json")}});
}

RequestID GoogleAIClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    RequestID id = createRequest();

    QString resolved = endpoint;
    if (resolved.isEmpty()) {
        const QString modelName = payload.contains("model") ? payload["model"].toString() : model();
        const QString suffix = (mode == RequestMode::Streaming)
                                   ? QStringLiteral(":streamGenerateContent?alt=sse")
                                   : QStringLiteral(":generateContent");
        resolved = QStringLiteral("/models/%1%2").arg(modelName, suffix);
    }
    qCDebug(llmGoogleLog).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(url() + resolved), payload, mode);
    return id;
}

RequestID GoogleAIClient::ask(const QString &prompt, RequestMode mode)
{
    QJsonObject payload;
    payload["contents"] = QJsonArray{
        QJsonObject{{"role", "user"}, {"parts", QJsonArray{QJsonObject{{"text", prompt}}}}}};

    return sendMessage(payload, {}, mode);
}

QJsonObject GoogleAIClient::buildConversationPayload(const Conversation &conversation) const
{
    QJsonObject payload;

    if (!conversation.system().isEmpty()) {
        payload["systemInstruction"] = QJsonObject{
            {"parts", QJsonArray{QJsonObject{{"text", conversation.system()}}}}};
    }

    QJsonArray contents;
    for (const Turn &turn : conversation.turns()) {
        QJsonArray parts;

        if (turn.role == TurnRole::Tool) {
            for (const TurnContent &block : turn.content) {
                const auto *result = std::get_if<ToolResultContent>(&block);
                if (!result)
                    continue;

                const ToolResult toolResult = toToolResult(*result);
                QJsonObject functionResponse = {
                    {"name", result->name},
                    {"response", QJsonObject{{"result", toolResultText(toolResult)}}}};

                if (!toolResult.hasOnlyText()) {
                    QJsonArray media;
                    for (const ToolContent &part : toolResult.content) {
                        const QJsonObject inlinePart = GoogleMessage::toInlineDataPart(part);
                        if (!inlinePart.isEmpty())
                            media.append(inlinePart);
                    }
                    if (!media.isEmpty())
                        functionResponse["parts"] = media;
                }

                parts.append(QJsonObject{{"functionResponse", functionResponse}});
            }
            if (!parts.isEmpty())
                contents.append(
                    QJsonObject{
                        {"role", GoogleMessage::toolResultTurnRole(parts)}, {"parts", parts}});
            continue;
        }

        const QJsonObject serialized = GoogleMessage::serializeTurn(turn.role, turn.content);
        if (serialized.value("parts").toArray().isEmpty())
            continue;

        contents.append(serialized);
    }

    payload["contents"] = contents;
    return payload;
}

const ToolDialect &GoogleAIClient::toolDialect() const
{
    return GoogleMessage::toolDialect();
}

const UsageSchema &GoogleAIClient::usageSchema() const
{
    return kGoogleUsage;
}

QFuture<QList<ModelInfo>> GoogleAIClient::listModels(const QString &endpoint)
{
    return fetchModelList(
        endpointUrl(endpoint, QStringLiteral("/models")),
        QStringLiteral("models"),
        QStringLiteral("name"),
        [](QString name) {
            return name.contains('/') ? name.split('/').last() : name;
        });
}

QList<BaseClient::ErrorAnnotation> GoogleAIClient::errorAnnotations() const
{
    return {
        {QStringLiteral("code"), QStringLiteral("code")},
        {QStringLiteral("status"), QStringLiteral("status")}};
}

std::optional<QString> GoogleAIClient::JsonErrorSniffer::append(const QByteArray &chunk)
{
    if (!m_active)
        return std::nullopt;

    m_buffer.append(chunk);

    const QByteArray trimmed = m_buffer.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;

    // SSE framing never starts with '{': stop sniffing for the rest of the stream.
    if (!trimmed.startsWith('{') || m_buffer.size() > kMaxBytes) {
        m_active = false;
        m_buffer.clear();
        return std::nullopt;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
    if (doc.isNull() || !doc.isObject())
        return std::nullopt;

    m_active = false;
    m_buffer.clear();

    const QJsonObject obj = doc.object();
    if (!obj.contains("error"))
        return std::nullopt;

    const QJsonObject error = obj.value("error").toObject();
    return QString("Google AI API Error %1: %2")
        .arg(error.value("code").toInt())
        .arg(error.value("message").toString());
}

void GoogleAIClient::processData(const RequestID &id, const QByteArray &data)
{
    if (data.isEmpty())
        return;

    if (!hasRequest(id))
        return;

    if (const auto error = m_errorSniffers[id].append(data)) {
        qCDebug(llmGoogleLog).noquote() << *error;
        m_failedRequests.insert(id, *error);
        return;
    }

    BaseClient::processData(id, data);
}

void GoogleAIClient::processSseEvent(
    const RequestID &id, const SSEEvent &, const QJsonObject &chunk)
{
    processStreamChunk(id, chunk);
}

std::optional<QString> GoogleAIClient::takePendingStreamError(const RequestID &id)
{
    if (!m_failedRequests.contains(id))
        return std::nullopt;

    return m_failedRequests.take(id);
}

void GoogleAIClient::onStreamDrained(const RequestID &id)
{
    notifyPendingThinkingBlocks(id);
    executeToolsFromMessage(id);
}

void GoogleAIClient::processStreamChunk(const RequestID &id, const QJsonObject &chunk)
{
    applyUsage(id, chunk);

    if (!chunk.contains("candidates"))
        return;

    GoogleMessage *message = ensureMessage<GoogleMessage>(id);

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

                        message->handleToolCallStart(name);
                        message->handleToolCallDelta(
                            QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
                        message->handleToolCallComplete();
                    }
                }
            }
        }

        if (candidateObj.contains("finishReason")) {
            QString finishReason = candidateObj["finishReason"].toString();
            message->handleStopReason(finishReason);

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

void GoogleAIClient::cleanupDerivedData(const RequestID &id)
{
    m_failedRequests.remove(id);
    m_errorSniffers.remove(id);
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

    const QJsonArray toolResultParts = googleMsg->createToolResultParts(toolResults);

    QJsonObject functionMessage;
    functionMessage["role"] = GoogleMessage::toolResultTurnRole(toolResultParts);
    functionMessage["parts"] = toolResultParts;
    contents.append(functionMessage);

    request["contents"] = contents;
    return request;
}

void GoogleAIClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    processStreamChunk(id, response);
}

} // namespace LLMQore
