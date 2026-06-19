// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/RpcPipeTransport.hpp>

namespace LLMQore::Rpc {

PipeTransport::PipeTransport(QObject *parent)
    : Transport(parent)
{}

PipeTransport::~PipeTransport()
{
    stop();
}

std::pair<PipeTransport *, PipeTransport *> PipeTransport::createPair(QObject *parent)
{
    auto *a = new PipeTransport(parent);
    auto *b = new PipeTransport(parent);
    a->m_peer = b;
    b->m_peer = a;
    return {a, b};
}

void PipeTransport::start()
{
    if (m_open)
        return;
    m_open = true;
    setState(State::Connected);
}

void PipeTransport::stop()
{
    if (!m_open)
        return;
    m_open = false;
    setState(State::Disconnected);
    emit closed();
}

void PipeTransport::send(const QJsonObject &message)
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

void PipeTransport::deliver(const QJsonObject &message)
{
    emit messageReceived(message);
}

} // namespace LLMQore::Rpc
