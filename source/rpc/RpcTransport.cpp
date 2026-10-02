// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/RpcTransport.hpp>

namespace LLMQore::Rpc {

Transport::Transport(QObject *parent)
    : QObject(parent)
{}

void Transport::abandon(const QString &requestId)
{
    Q_UNUSED(requestId)
}

} // namespace LLMQore::Rpc
