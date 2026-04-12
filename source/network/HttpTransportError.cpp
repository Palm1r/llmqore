// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/HttpTransportError.hpp>

#include <utility>

namespace LLMCore {

HttpTransportError::HttpTransportError(QString message, QNetworkReply::NetworkError code)
    : m_message(std::move(message))
    , m_stdMessage(m_message.toStdString())
    , m_code(code)
{}

} // namespace LLMCore
