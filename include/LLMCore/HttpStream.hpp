// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

#include <LLMCore/HttpTransportError.hpp>
#include <LLMCore/LLMCore_global.h>

class QNetworkReply;

namespace LLMCore {

class HttpClient;

class LLMCORE_EXPORT HttpStream : public QObject
{
    Q_OBJECT
public:
    ~HttpStream() override;

    [[nodiscard]] int statusCode() const;
    [[nodiscard]] QList<QPair<QByteArray, QByteArray>> rawHeaders() const;
    [[nodiscard]] QByteArray rawHeader(QByteArrayView name) const;
    [[nodiscard]] QString contentType() const;

    [[nodiscard]] bool isFinished() const noexcept;

    void abort();

signals:
    void headersReceived();
    void chunkReceived(QByteArray chunk);
    void finished();
    void errorOccurred(LLMCore::HttpTransportError error);

private:
    friend class HttpClient;
    explicit HttpStream(QNetworkReply *reply, QObject *parent = nullptr);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMCore
