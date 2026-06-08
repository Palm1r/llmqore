// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <memory>

#include <QByteArray>
#include <QByteArrayView>
#include <QFuture>
#include <QObject>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/LLMQore_global.h>

class QNetworkProxy;
class QNetworkRequest;

namespace LLMQore {

class HttpStream;

class LLMQORE_EXPORT HttpClient : public QObject
{
    Q_OBJECT
public:
    static constexpr int DefaultTransferTimeoutMs = 120000;

    explicit HttpClient(QObject *parent = nullptr);
    ~HttpClient() override;

    [[nodiscard]] QFuture<HttpResponse> send(
        const QNetworkRequest &request,
        QByteArrayView verb,
        const QByteArray &body = {});

    [[nodiscard]] HttpStream *openStream(
        const QNetworkRequest &request,
        QByteArrayView verb,
        const QByteArray &body = {});

    void setProxy(const QNetworkProxy &proxy);

    void setTransferTimeout(int milliseconds);
    void setTransferTimeout(std::chrono::milliseconds timeout);

    [[nodiscard]] int transferTimeoutMs() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore
