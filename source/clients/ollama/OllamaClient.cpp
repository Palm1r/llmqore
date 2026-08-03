// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OllamaClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "OllamaMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/RpcLineFramer.hpp>
#include <LLMQore/Log.hpp>

#include "core/ThreadAffinity.hpp"

namespace LLMQore {

namespace {

const UsageSchema kOllamaUsage{
    {},
    {{}, QLatin1String("prompt_eval_count")},
    {{}, QLatin1String("eval_count")},
    {},
    {}};

} // namespace

OllamaClient::OllamaClient(QObject *parent)
    : OllamaClient({}, {}, {}, parent)
{}

OllamaClient::OllamaClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OllamaClient(url, apiKey, model, nullptr, parent)
{}

OllamaClient::OllamaClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : BaseClient(url, apiKey, model, transport, parent)
{
    setLogCategory(llmOllamaLog());
    setAuthScheme(
        {.placement = AuthScheme::Placement::Header,
         .name = QStringLiteral("Authorization"),
         .valuePrefix = QStringLiteral("Bearer ")});
    setHeaders({{QStringLiteral("Content-Type"), QStringLiteral("application/json")}});
}

RequestID OllamaClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    LLMQORE_ASSERT_OWNING_THREAD();
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

QJsonObject OllamaClient::buildConversationPayload(const Conversation &conversation) const
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
                            {"content", toolResultText(toToolResult(*result))}});
                }
            }
            continue;
        }

        messages.append(OllamaMessage::serializeTurn(turn.role, turn.content));
    }

    payload["messages"] = messages;
    return payload;
}

const ToolDialect &OllamaClient::toolDialect() const
{
    return OllamaMessage::toolDialect();
}

const UsageSchema &OllamaClient::usageSchema() const
{
    return kOllamaUsage;
}

QFuture<QList<ModelInfo>> OllamaClient::listModels(const QString &endpoint)
{
    return fetchModelList(
        endpointUrl(endpoint, QStringLiteral("/api/tags")),
        QStringLiteral("models"),
        QStringLiteral("name"));
}

void OllamaClient::processData(const RequestID &id, const QByteArray &data)
{
    if (data.isEmpty())
        return;

    if (!hasRequest(id))
        return;

    const QByteArrayList lines = requestLineFramer(id).append(data);

    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty())
            continue;

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (doc.isNull() || !doc.isObject()) {
            qCDebug(llmOllamaLog).noquote()
                << QString("Failed to parse JSON: %1").arg(error.errorString());
            continue;
        }

        if (!handleStreamObject(id, doc.object()))
            return;
    }
}

bool OllamaClient::handleStreamObject(const RequestID &id, const QJsonObject &obj)
{
    const QString errorMsg = obj.value("error").toString();
    if (!errorMsg.isEmpty()) {
        qCWarning(llmOllamaLog).noquote() << "Error in response: " + errorMsg;
        cleanupFullRequest(id);
        failRequest(id, errorMsg);
        return false;
    }

    processStreamData(id, obj);
    return true;
}

QJsonObject OllamaClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    return appendChatContinuation<OllamaMessage>(originalPayload, message, toolResults);
}

void OllamaClient::flushStreamBuffers(const RequestID &id)
{
    Rpc::LineFramer &framer = requestLineFramer(id);
    if (!framer.hasIncompleteData())
        return;

    const QByteArray remaining = framer.currentBuffer().trimmed();
    framer.clear();
    if (remaining.isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(remaining);
    if (doc.isNull() || !doc.isObject())
        return;

    handleStreamObject(id, doc.object());
}

void OllamaClient::processStreamData(const RequestID &id, const QJsonObject &data)
{
    OllamaMessage *message = ensureMessage<OllamaMessage>(id);

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
                notifyPendingThinkingBlocks(id);
                message->handleContentDelta(content);
                if (!message->isAccumulatingToolCall())
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

        message->handleDone(true, data.value("done_reason").toString());

        applyUsage(id, data);

        notifyPendingThinkingBlocks(id);
        executeToolsFromMessage(id);
    }
}

void OllamaClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    processStreamData(id, response);
}

} // namespace LLMQore
