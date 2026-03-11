// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/HttpClient.hpp>

#include <QJsonDocument>
#include <QNetworkProxy>

namespace LLMCore {

HttpClient::HttpClient(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{}

void HttpClient::setTransferTimeout(int ms)
{
    m_transferTimeoutMs = ms;
}

QFuture<QByteArray> HttpClient::get(const QNetworkRequest &request)
{
    auto promise = std::make_shared<QPromise<QByteArray>>();
    promise->start();

    QNetworkRequest req(request);
    req.setTransferTimeout(m_transferTimeoutMs);
    QNetworkReply *reply = m_manager->get(req);
    setupReply(reply, promise, RequestMode::Buffered);

    return promise->future();
}

QFuture<QByteArray> HttpClient::post(
    const QNetworkRequest &request, const QJsonObject &payload, RequestMode mode)
{
    QJsonDocument doc(payload);

    auto promise = std::make_shared<QPromise<QByteArray>>();
    promise->start();

    QNetworkRequest req(request);
    req.setTransferTimeout(m_transferTimeoutMs);
    QNetworkReply *reply = m_manager->post(req, doc.toJson(QJsonDocument::Compact));
    setupReply(reply, promise, mode);

    return promise->future();
}

QFuture<QByteArray> HttpClient::del(
    const QNetworkRequest &request, std::optional<QJsonObject> payload)
{
    auto promise = std::make_shared<QPromise<QByteArray>>();
    promise->start();

    QNetworkRequest req(request);
    req.setTransferTimeout(m_transferTimeoutMs);
    QNetworkReply *reply;
    if (payload) {
        QJsonDocument doc(*payload);
        reply = m_manager->sendCustomRequest(req, "DELETE", doc.toJson(QJsonDocument::Compact));
    } else {
        reply = m_manager->deleteResource(req);
    }

    setupReply(reply, promise, RequestMode::Buffered);

    return promise->future();
}

void HttpClient::setProxy(const QNetworkProxy &proxy)
{
    m_manager->setProxy(proxy);
}

void HttpClient::setupReply(
    QNetworkReply *reply, std::shared_ptr<QPromise<QByteArray>> promise, RequestMode mode)
{
    if (mode == RequestMode::Streaming) {
        connect(reply, &QNetworkReply::readyRead, this, [reply, promise]() {
            QByteArray data = reply->readAll();
            if (!data.isEmpty())
                promise->addResult(data);
        });
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, promise]() {
        handleFinished(reply, promise);
    });
}

void HttpClient::handleFinished(QNetworkReply *reply, std::shared_ptr<QPromise<QByteArray>> promise)
{
    QByteArray remaining = reply->readAll();
    QNetworkReply::NetworkError networkError = reply->error();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString errorString = reply->errorString();

    reply->disconnect();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError) {
        QString errorMsg = parseErrorFromResponse(statusCode, remaining, errorString);
        promise->setException(std::make_exception_ptr(std::runtime_error(errorMsg.toStdString())));
    } else if (!remaining.isEmpty()) {
        promise->addResult(remaining);
    }

    promise->finish();
}

QString HttpClient::parseErrorFromResponse(
    int statusCode, const QByteArray &responseBody, const QString &networkErrorString)
{
    if (!responseBody.isEmpty()) {
        QJsonDocument errorDoc = QJsonDocument::fromJson(responseBody);
        if (!errorDoc.isNull() && errorDoc.isObject()) {
            QJsonObject errorObj = errorDoc.object();
            if (errorObj.contains("error")) {
                QJsonValue errorValue = errorObj["error"];
                if (errorValue.isString()) {
                    return QString("HTTP %1: %2").arg(statusCode).arg(errorValue.toString());
                }
                QJsonObject error = errorValue.toObject();
                QString message = error["message"].toString();
                QString type = error["type"].toString();
                QString code = error["code"].toString();

                QString errorMsg = QString("HTTP %1: %2").arg(statusCode).arg(message);
                if (!type.isEmpty())
                    errorMsg += QString(" (type: %1)").arg(type);
                if (!code.isEmpty())
                    errorMsg += QString(" (code: %1)").arg(code);
                return errorMsg;
            }
            return QString("HTTP %1: %2").arg(statusCode).arg(QString::fromUtf8(responseBody));
        }
        return QString("HTTP %1: %2").arg(statusCode).arg(QString::fromUtf8(responseBody));
    }
    return QString("HTTP %1: %2").arg(statusCode).arg(networkErrorString);
}

} // namespace LLMCore
