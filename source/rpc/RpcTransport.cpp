// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/RpcTransport.hpp>

namespace LLMQore::Rpc {

Transport::Transport(QObject *parent)
    : QObject(parent)
{}

void Transport::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace LLMQore::Rpc
