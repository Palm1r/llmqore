// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <memory>

#include <QByteArray>
#include <QByteArrayView>
#include <QFuture>
#include <QObject>

#include <LLMCore/HttpResponse.hpp>
#include <LLMCore/HttpTransportError.hpp>
#include <LLMCore/LLMCore_global.h>

class QNetworkProxy;
class QNetworkRequest;

namespace LLMCore {

class HttpStream;

class LLMCORE_EXPORT HttpClient : public QObject
{
    Q_OBJECT
public:
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

} // namespace LLMCore
