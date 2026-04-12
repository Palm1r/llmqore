// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/HttpClient.hpp>

#include <memory>

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPromise>

#include <LLMCore/HttpStream.hpp>
#include <LLMCore/Log.hpp>

namespace LLMCore {

namespace {

bool isTransportLevelError(QNetworkReply::NetworkError code) noexcept
{
    if (code == QNetworkReply::NoError)
        return false;
    return code < QNetworkReply::ContentAccessDenied;
}

QNetworkReply *dispatchVerb(
    QNetworkAccessManager *manager,
    const QNetworkRequest &request,
    QByteArrayView verb,
    const QByteArray &body)
{
    if (verb.compare(QByteArrayView("GET"), Qt::CaseInsensitive) == 0)
        return manager->get(request);
    if (verb.compare(QByteArrayView("POST"), Qt::CaseInsensitive) == 0)
        return manager->post(request, body);
    if (verb.compare(QByteArrayView("PUT"), Qt::CaseInsensitive) == 0)
        return manager->put(request, body);
    if (verb.compare(QByteArrayView("DELETE"), Qt::CaseInsensitive) == 0) {
        if (body.isEmpty())
            return manager->deleteResource(request);
        return manager->sendCustomRequest(request, "DELETE", body);
    }
    if (verb.compare(QByteArrayView("HEAD"), Qt::CaseInsensitive) == 0)
        return manager->head(request);
    return manager->sendCustomRequest(request, verb.toByteArray(), body);
}

} // namespace

struct HttpClient::Impl
{
    QNetworkAccessManager *manager = nullptr;
    int transferTimeoutMs = 120000;
};

HttpClient::HttpClient(QObject *parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->manager = new QNetworkAccessManager(this);
}

HttpClient::~HttpClient() = default;

QFuture<HttpResponse> HttpClient::send(
    const QNetworkRequest &request, QByteArrayView verb, const QByteArray &body)
{
    auto promise = std::make_shared<QPromise<HttpResponse>>();
    promise->start();

    QNetworkRequest req(request);
    req.setTransferTimeout(m_impl->transferTimeoutMs);

    QNetworkReply *reply = dispatchVerb(m_impl->manager, req, verb, body);

    connect(reply, &QNetworkReply::finished, this, [replyGuard = QPointer<QNetworkReply>(reply), promise]() {
        if (!replyGuard) {
            HttpTransportError err(
                QStringLiteral("Network reply destroyed before completion"),
                QNetworkReply::UnknownNetworkError);
            promise->setException(std::make_exception_ptr(err));
            promise->finish();
            return;
        }

        const QNetworkReply::NetworkError code = replyGuard->error();
        const QString errorString = replyGuard->errorString();
        const QByteArray body = replyGuard->readAll();
        const int statusCode
            = replyGuard->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto rawHeaderPairs = replyGuard->rawHeaderPairs();

        replyGuard->disconnect();
        replyGuard->deleteLater();

        if (isTransportLevelError(code)) {
            HttpTransportError err(errorString, code);
            qCDebug(llmNetworkLog).noquote() << "HttpClient transport error:" << err.message();
            promise->setException(std::make_exception_ptr(err));
            promise->finish();
            return;
        }

        HttpResponse response;
        response.statusCode = statusCode;
        response.rawHeaders = rawHeaderPairs;
        response.body = body;
        promise->addResult(std::move(response));
        promise->finish();
    });

    return promise->future();
}

HttpStream *HttpClient::openStream(
    const QNetworkRequest &request, QByteArrayView verb, const QByteArray &body)
{
    QNetworkRequest req(request);
    req.setTransferTimeout(m_impl->transferTimeoutMs);

    QNetworkReply *reply = dispatchVerb(m_impl->manager, req, verb, body);
    return new HttpStream(reply);
}

void HttpClient::setProxy(const QNetworkProxy &proxy)
{
    m_impl->manager->setProxy(proxy);
}

void HttpClient::setTransferTimeout(int milliseconds)
{
    m_impl->transferTimeoutMs = milliseconds;
}

void HttpClient::setTransferTimeout(std::chrono::milliseconds timeout)
{
    m_impl->transferTimeoutMs = static_cast<int>(timeout.count());
}

int HttpClient::transferTimeoutMs() const noexcept
{
    return m_impl->transferTimeoutMs;
}

} // namespace LLMCore
