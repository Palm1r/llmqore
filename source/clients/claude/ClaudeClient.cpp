// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/ClaudeClient.hpp>

#include <QJsonArray>
#include <QUrlQuery>

#include "ClaudeMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/SSEParser.hpp>

#include "core/ThreadAffinity.hpp"

namespace LLMQore {

ProviderProfile claudeProfile()
{
    return ProviderProfile{
        QStringLiteral("/v1/messages"),
        QStringLiteral("/v1/models"),
        &llmClaudeLog(),
        AuthScheme{AuthScheme::Placement::Header, QStringLiteral("x-api-key"), {}},
        {{QStringLiteral("Content-Type"), QStringLiteral("application/json")},
         {QStringLiteral("anthropic-version"), QStringLiteral("2023-06-01")}}};
}

namespace {

const UsageSchema kClaudeUsage{
    QLatin1String("usage"),
    {{}, QLatin1String("input_tokens")},
    {{}, QLatin1String("output_tokens")},
    {{}, QLatin1String("cache_read_input_tokens")},
    {}};

} // namespace

ClaudeClient::ClaudeClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : BaseClient(url, apiKey, model, transport, parent)
{
    setProfile(claudeProfile());
}

RequestID ClaudeClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);
    return postJson(request, endpoint, mode);
}

QJsonObject ClaudeClient::buildConversationPayload(const Conversation &conversation) const
{
    QJsonObject payload;
    payload["model"] = model();

    const std::optional<ModelInfo> known = cachedModel(model());
    const bool hasLimit = known && known->maxOutputTokens && *known->maxOutputTokens > 0;
    payload["max_tokens"] = hasLimit ? *known->maxOutputTokens : kDefaultMaxTokens;

    if (!conversation.system().isEmpty())
        payload["system"] = conversation.system();

    QJsonArray messages;
    for (const Turn &turn : conversation.turns()) {
        QJsonArray content;
        for (const TurnContent &block : turn.content)
            content.append(ClaudeMessage::serializeTurnContent(block));

        messages.append(
            QJsonObject{
                {"role",
                 turn.role == TurnRole::Assistant ? QStringLiteral("assistant")
                                                  : QStringLiteral("user")},
                {"content", content}});
    }
    payload["messages"] = messages;

    return payload;
}

const ToolDialect &ClaudeClient::toolDialect() const
{
    return ClaudeMessage::toolDialect();
}

const UsageSchema &ClaudeClient::usageSchema() const
{
    return kClaudeUsage;
}

QFuture<QList<ModelInfo>> ClaudeClient::listModels(const QString &endpoint)
{
    QUrl url = endpointUrl(endpoint, profile().modelsPath);
    QUrlQuery query;
    query.addQueryItem("limit", "1000");
    url.setQuery(query);

    return fetchModelList(
        url,
        QStringLiteral("data"),
        QStringLiteral("id"),
        {},
        [](const QJsonObject &entry, ModelInfo &info) {
            info.displayName = entry.value("display_name").toString();

            const auto positiveInt = [&entry](const QString &key) -> std::optional<int> {
                const QJsonValue value = entry.value(key);
                if (!value.isDouble())
                    return std::nullopt;
                const int number = value.toInt(0);
                if (number <= 0)
                    return std::nullopt;
                return number;
            };

            info.maxOutputTokens = positiveInt(QStringLiteral("max_tokens"));
            info.maxInputTokens = positiveInt(QStringLiteral("max_input_tokens"));

            const QJsonObject capabilities = entry.value("capabilities").toObject();
            if (capabilities.isEmpty())
                return;

            const auto supported = [&capabilities](const QString &key) {
                return capabilities.value(key).toObject().value("supported").toBool();
            };

            info.supportsImageInput = supported(QStringLiteral("image_input"));
            info.supportsThinking = supported(QStringLiteral("thinking"));
            info.supportsToolCalls = supported(QStringLiteral("tool_use"));
            info.supportsStructuredOutputs = supported(QStringLiteral("structured_outputs"));
        });
}

QList<BaseClient::ErrorAnnotation> ClaudeClient::errorAnnotations() const
{
    return {{{}, QStringLiteral("type")}};
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

void ClaudeClient::processSseEvent(const RequestID &id, const SSEEvent &, const QJsonObject &event)
{
    const QString eventType = event["type"].toString();

    if (eventType == "message_stop")
        return;

    auto *message = qobject_cast<ClaudeMessage *>(messageForRequest(id));
    if (!message) {
        if (eventType != "message_start") {
            qCWarning(llmClaudeLog).noquote()
                << QString("Dropping event '%1' for request %2: no active message (missing "
                           "message_start?)")
                       .arg(eventType, id);
            return;
        }
        message = ensureMessage<ClaudeMessage>(id);
    }

    applyEffects(id, message->applyEvent(event));
}

void ClaudeClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    applyEffects(id, ensureMessage<ClaudeMessage>(id)->applyResponse(response));
}

} // namespace LLMQore
