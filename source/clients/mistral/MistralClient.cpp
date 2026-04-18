// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/MistralClient.hpp>

#include <LLMQore/Log.hpp>

namespace LLMQore {

MistralClient::MistralClient(QObject *parent)
    : MistralClient({}, {}, {}, parent)
{}

MistralClient::MistralClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OpenAIClient(url, apiKey, model, parent)
{}

RequestID MistralClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    const RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/chat/completions") : endpoint;

    qCDebug(llmMistralLog).noquote()
        << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

} // namespace LLMQore
