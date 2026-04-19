// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/MistralClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include <LLMQore/HttpClient.hpp>
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
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/v1/chat/completions") : endpoint;

    qCDebug(llmMistralLog).noquote()
        << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

QFuture<QList<QString>> MistralClient::listModels()
{
    QUrl url(m_url + "/v1/models");
    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->send(request, QByteArrayView("GET"))
        .then(this, [](const HttpResponse &response) {
            QList<QString> models;
            if (!response.isSuccess())
                return models;

            const QJsonObject json = QJsonDocument::fromJson(response.body).object();
            if (json.contains("data")) {
                const QJsonArray modelArray = json["data"].toArray();
                for (const QJsonValue &value : modelArray) {
                    const QJsonObject modelObject = value.toObject();
                    if (modelObject.contains("id"))
                        models.append(modelObject["id"].toString());
                }
            }
            return models;
        })
        .onFailed(this, [](const std::exception &) { return QList<QString>{}; });
}

} // namespace LLMQore
