// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QFuture>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPromise>

#include <LLMCore/LLMCore_global.h>

#include <LLMCore/RequestMode.hpp>

namespace LLMCore {

class LLMCORE_EXPORT HttpClient : public QObject
{
    Q_OBJECT

public:
    explicit HttpClient(QObject *parent = nullptr);

    QFuture<QByteArray> get(const QNetworkRequest &request);
    QFuture<QByteArray> post(
        const QNetworkRequest &request,
        const QJsonObject &payload,
        RequestMode mode = RequestMode::Buffered);
    QFuture<QByteArray> del(
        const QNetworkRequest &request, std::optional<QJsonObject> payload = std::nullopt);

    void setProxy(const QNetworkProxy &proxy);
    void setTransferTimeout(int ms);

private:
    void setupReply(
        QNetworkReply *reply, std::shared_ptr<QPromise<QByteArray>> promise, RequestMode mode);
    void handleFinished(QNetworkReply *reply, std::shared_ptr<QPromise<QByteArray>> promise);
    QString parseErrorFromResponse(
        int statusCode, const QByteArray &responseBody, const QString &networkErrorString);

    QNetworkAccessManager *m_manager;
    int m_transferTimeoutMs = 120000;
};

} // namespace LLMCore
