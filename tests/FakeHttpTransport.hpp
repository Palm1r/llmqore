// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <QByteArray>
#include <QByteArrayView>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QPointer>
#include <QPromise>
#include <QUrl>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>

namespace LLMQoreTest {

struct SentRequest
{
    QNetworkRequest request;
    QByteArray verb;
    QByteArray body;

    QUrl url() const { return request.url(); }
    QByteArray header(const QByteArray &name) const { return request.rawHeader(name); }
    QJsonObject payload() const { return QJsonDocument::fromJson(body).object(); }
};

class FakeHttpStream : public LLMQore::HttpStreamHandle
{
    Q_OBJECT
public:
    explicit FakeHttpStream(SentRequest sent, QObject *parent = nullptr)
        : LLMQore::HttpStreamHandle(parent)
        , m_sent(std::move(sent))
    {}

    const SentRequest &sent() const { return m_sent; }

    int statusCode() const override { return m_statusCode; }
    QList<QPair<QByteArray, QByteArray>> rawHeaders() const override { return m_rawHeaders; }
    void abort() override { m_aborted = true; }

    bool isAborted() const { return m_aborted; }

    void sendHeaders(
        int statusCode = 200, const QList<QPair<QByteArray, QByteArray>> &headers = {})
    {
        m_statusCode = statusCode;
        m_rawHeaders = headers.isEmpty()
            ? QList<QPair<QByteArray, QByteArray>>{{"Content-Type", "text/event-stream"}}
            : headers;
        emit headersReceived();
    }

    void sendChunk(const QByteArray &chunk) { emit chunkReceived(chunk); }

    void sendFinished() { emit finished(); }

    void sendError(
        const QString &message,
        QNetworkReply::NetworkError code = QNetworkReply::UnknownNetworkError)
    {
        emit errorOccurred(LLMQore::HttpTransportError(message, code));
    }

    void sendAll(const QByteArray &chunk, int statusCode = 200)
    {
        sendHeaders(statusCode);
        sendChunk(chunk);
        sendFinished();
    }

private:
    SentRequest m_sent;
    int m_statusCode = 0;
    QList<QPair<QByteArray, QByteArray>> m_rawHeaders;
    bool m_aborted = false;
};

class FakeHttpTransport : public LLMQore::HttpTransport
{
    Q_OBJECT
public:
    explicit FakeHttpTransport(QObject *parent = nullptr)
        : LLMQore::HttpTransport(parent)
    {}

    QFuture<LLMQore::HttpResponse> send(
        const QNetworkRequest &request, QByteArrayView verb, const QByteArray &body = {}) override
    {
        BufferedCall call;
        call.sent = SentRequest{request, verb.toByteArray(), body};
        call.promise = std::make_shared<QPromise<LLMQore::HttpResponse>>();
        call.promise->start();
        m_buffered.append(call);
        return call.promise->future();
    }

    LLMQore::HttpStreamHandle *openStream(
        const QNetworkRequest &request, QByteArrayView verb, const QByteArray &body = {}) override
    {
        if (!m_refusedVerb.isEmpty() && verb.toByteArray() == m_refusedVerb) {
            ++m_refusedStreams;
            return nullptr;
        }
        const SentRequest sent{request, verb.toByteArray(), body};
        auto *stream = new FakeHttpStream(sent, this);
        m_streams.append(stream);
        m_streamRequests.append(sent);
        return stream;
    }

    int streamCount() const { return m_streams.size(); }

    void refuseStreams(const QByteArray &verb) { m_refusedVerb = verb; }
    int refusedStreams() const { return m_refusedStreams; }

    FakeHttpStream *lastStream() const
    {
        return m_streams.isEmpty() ? nullptr : m_streams.last().data();
    }

    FakeHttpStream *streamAt(int index) const { return m_streams.value(index).data(); }

    SentRequest streamRequest(int index) const { return m_streamRequests.value(index); }

    SentRequest lastStreamRequest(const QByteArray &verb) const
    {
        for (auto it = m_streamRequests.crbegin(); it != m_streamRequests.crend(); ++it) {
            if (it->verb == verb)
                return *it;
        }
        return {};
    }

    void respondToStream(
        int index,
        int statusCode,
        const QByteArray &body,
        const QList<QPair<QByteArray, QByteArray>> &headers = {})
    {
        FakeHttpStream *stream = streamAt(index);
        if (!stream)
            return;
        const QList<QPair<QByteArray, QByteArray>> json{{"Content-Type", "application/json"}};
        stream->sendHeaders(statusCode, headers.isEmpty() ? json : headers);
        if (!body.isEmpty())
            stream->sendChunk(body);
        stream->sendFinished();
    }

    void respondToLastStream(
        int statusCode,
        const QByteArray &body,
        const QList<QPair<QByteArray, QByteArray>> &headers = {})
    {
        respondToStream(streamCount() - 1, statusCode, body, headers);
    }

    void failLastStream(
        const QString &message,
        QNetworkReply::NetworkError code = QNetworkReply::UnknownNetworkError)
    {
        if (FakeHttpStream *stream = lastStream())
            stream->sendError(message, code);
    }

    int bufferedCount() const { return m_buffered.size(); }

    SentRequest bufferedRequest(int index) const { return m_buffered.value(index).sent; }

    void respondTo(
        int index,
        int statusCode,
        const QByteArray &body,
        const QList<QPair<QByteArray, QByteArray>> &headers = {})
    {
        if (index < 0 || index >= m_buffered.size())
            return;
        LLMQore::HttpResponse response;
        response.statusCode = statusCode;
        response.rawHeaders = headers.isEmpty()
            ? QList<QPair<QByteArray, QByteArray>>{{"Content-Type", "application/json"}}
            : headers;
        response.body = body;
        auto &promise = m_buffered[index].promise;
        promise->addResult(response);
        promise->finish();
    }

    void respondToLast(
        int statusCode,
        const QByteArray &body,
        const QList<QPair<QByteArray, QByteArray>> &headers = {})
    {
        respondTo(m_buffered.size() - 1, statusCode, body, headers);
    }

    void failLast(
        const QString &message,
        QNetworkReply::NetworkError code = QNetworkReply::UnknownNetworkError)
    {
        if (m_buffered.isEmpty())
            return;
        auto &promise = m_buffered.last().promise;
        promise->setException(
            std::make_exception_ptr(LLMQore::HttpTransportError(message, code)));
        promise->finish();
    }

private:
    struct BufferedCall
    {
        SentRequest sent;
        std::shared_ptr<QPromise<LLMQore::HttpResponse>> promise;
    };

    QList<QPointer<FakeHttpStream>> m_streams;
    QList<SentRequest> m_streamRequests;
    QList<BufferedCall> m_buffered;
    QByteArray m_refusedVerb;
    int m_refusedStreams = 0;
};

inline bool waitForStreams(const FakeHttpTransport &transport, int count, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (transport.streamCount() < count && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return transport.streamCount() >= count;
}

} // namespace LLMQoreTest
