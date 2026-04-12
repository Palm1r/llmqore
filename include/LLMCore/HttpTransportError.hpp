// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include <QException>
#include <QNetworkReply>
#include <QString>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore {

class LLMCORE_EXPORT HttpTransportError : public QException
{
public:
    HttpTransportError(QString message, QNetworkReply::NetworkError code);

    void raise() const override { throw *this; }
    HttpTransportError *clone() const override { return new HttpTransportError(*this); }
    const char *what() const noexcept override { return m_stdMessage.c_str(); }

    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] QNetworkReply::NetworkError networkError() const noexcept { return m_code; }

private:
    QString m_message;
    std::string m_stdMessage;
    QNetworkReply::NetworkError m_code;
};

} // namespace LLMCore
