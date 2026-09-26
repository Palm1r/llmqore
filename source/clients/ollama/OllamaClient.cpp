// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/OllamaClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "OllamaMessage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

ProviderProfile ollamaProfile()
{
    ProviderProfile profile = {
        .chatPath = QStringLiteral("/api/chat"),
        .modelsPath = QStringLiteral("/api/tags"),
        .log = &llmOllamaLog()};
    profile.auth = {
        .placement = AuthScheme::Placement::Header,
        .name = QStringLiteral("Authorization"),
        .valuePrefix = QStringLiteral("Bearer ")};
    return profile;
}

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
    setProfile(ollamaProfile());
}

RequestID OllamaClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);
    return postJson(request, endpoint, mode);
}

QJsonObject OllamaClient::buildConversationPayload(const Conversation &conversation) const
{
    QJsonObject payload;
    payload["model"] = model();

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
        endpointUrl(endpoint, profile().modelsPath),
        QStringLiteral("models"),
        QStringLiteral("name"));
}

StreamFraming OllamaClient::streamFraming() const
{
    return StreamFraming::JsonLines;
}

void OllamaClient::processJsonLine(const RequestID &id, const QJsonObject &json)
{
    applyEffects(id, ensureMessage<OllamaMessage>(id)->applyEvent(json));
}

QJsonObject OllamaClient::buildContinuationPayload(
    const QJsonObject &originalPayload,
    BaseMessage *message,
    const QHash<QString, ToolResult> &toolResults)
{
    return appendChatContinuation<OllamaMessage>(originalPayload, message, toolResults);
}

void OllamaClient::processBufferedBody(const RequestID &id, const QJsonObject &response)
{
    applyEffects(id, ensureMessage<OllamaMessage>(id)->applyResponse(response));
}

} // namespace LLMQore
