// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpPipeTransport.hpp>

namespace LLMCore::Mcp {

McpPipeTransport::McpPipeTransport(QObject *parent)
    : McpTransport(parent)
{}

McpPipeTransport::~McpPipeTransport()
{
    stop();
}

std::pair<McpPipeTransport *, McpPipeTransport *> McpPipeTransport::createPair(QObject *parent)
{
    auto *a = new McpPipeTransport(parent);
    auto *b = new McpPipeTransport(parent);
    a->m_peer = b;
    b->m_peer = a;
    return {a, b};
}

void McpPipeTransport::start()
{
    if (m_open)
        return;
    m_open = true;
    setState(State::Connected);
}

void McpPipeTransport::stop()
{
    if (!m_open)
        return;
    m_open = false;
    setState(State::Disconnected);
    emit closed();
}

void McpPipeTransport::send(const QJsonObject &message)
{
    if (!m_open)
        return;
    if (!m_peer || !m_peer->m_open) {
        emit errorOccurred(QStringLiteral("Pipe peer not open"));
        return;
    }
    QMetaObject::invokeMethod(
        m_peer.data(),
        "deliver",
        Qt::QueuedConnection,
        Q_ARG(QJsonObject, message));
}

void McpPipeTransport::deliver(const QJsonObject &message)
{
    emit messageReceived(message);
}

} // namespace LLMCore::Mcp
