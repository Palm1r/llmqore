// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/HttpStream.hpp>

#include <LLMCore/Log.hpp>

#include <QNetworkReply>
#include <QPointer>

namespace LLMCore {

namespace {

bool headerNameMatches(const QByteArray &have, QByteArrayView want) noexcept
{
    return QByteArrayView(have).compare(want, Qt::CaseInsensitive) == 0;
}

bool isTransportLevelError(QNetworkReply::NetworkError code) noexcept
{
    if (code == QNetworkReply::NoError)
        return false;
    return code < QNetworkReply::ContentAccessDenied;
}

} // namespace

struct HttpStream::Impl
{
    QNetworkReply *reply = nullptr;
    bool headersFired = false;
    bool terminalFired = false;
    bool aborted = false;
    int statusCode = 0;
    QList<QPair<QByteArray, QByteArray>> rawHeaders;
};

HttpStream::HttpStream(QNetworkReply *reply, QObject *parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->reply = reply;
    reply->setParent(this);

    auto fireHeaders = [this]() {
        if (m_impl->headersFired || m_impl->aborted)
            return;
        m_impl->headersFired = true;
        m_impl->statusCode
            = m_impl->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_impl->rawHeaders = m_impl->reply->rawHeaderPairs();
        emit headersReceived();
    };

    connect(reply, &QNetworkReply::metaDataChanged, this, fireHeaders);

    connect(reply, &QNetworkReply::readyRead, this, [this, fireHeaders]() {
        if (m_impl->aborted || m_impl->terminalFired)
            return;
        if (!m_impl->headersFired)
            fireHeaders();
        const QByteArray chunk = m_impl->reply->readAll();
        if (!chunk.isEmpty())
            emit chunkReceived(chunk);
    });

    connect(reply, &QNetworkReply::finished, this, [this, fireHeaders]() {
        if (m_impl->aborted || m_impl->terminalFired)
            return;

        const QByteArray trailing = m_impl->reply->readAll();
        if (!trailing.isEmpty()) {
            if (!m_impl->headersFired)
                fireHeaders();
            emit chunkReceived(trailing);
        }

        const QNetworkReply::NetworkError code = m_impl->reply->error();

        if (isTransportLevelError(code)) {
            m_impl->terminalFired = true;
            HttpTransportError err(m_impl->reply->errorString(), code);
            qCDebug(llmNetworkLog).noquote() << "HttpStream transport error:" << err.message();
            emit errorOccurred(err);
            return;
        }

        // For tiny responses, metaDataChanged and finished can collapse
        // into a single event-loop iteration, so readyRead never fires.
        if (!m_impl->headersFired)
            fireHeaders();

        m_impl->terminalFired = true;
        emit finished();
    });
}

HttpStream::~HttpStream()
{
    if (m_impl->reply && !m_impl->aborted) {
        m_impl->reply->disconnect(this);
        m_impl->reply->abort();
    }
}

int HttpStream::statusCode() const
{
    return m_impl->statusCode;
}

QList<QPair<QByteArray, QByteArray>> HttpStream::rawHeaders() const
{
    return m_impl->rawHeaders;
}

QByteArray HttpStream::rawHeader(QByteArrayView name) const
{
    for (const auto &pair : m_impl->rawHeaders) {
        if (headerNameMatches(pair.first, name))
            return pair.second;
    }
    return {};
}

QString HttpStream::contentType() const
{
    const QByteArray value = rawHeader(QByteArrayView("Content-Type"));
    if (value.isEmpty())
        return {};
    return QString::fromUtf8(value).toLower();
}

bool HttpStream::isFinished() const noexcept
{
    return m_impl->terminalFired || m_impl->aborted;
}

void HttpStream::abort()
{
    if (m_impl->aborted)
        return;
    m_impl->aborted = true;
    if (m_impl->reply) {
        m_impl->reply->disconnect(this);
        m_impl->reply->abort();
    }
}

} // namespace LLMCore
