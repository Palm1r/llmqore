// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/ToolLoopRunner.hpp>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

ToolLoopRunner::ToolLoopRunner(BaseClient *client)
    : QObject(client)
    , m_client(client)
{
    connect(
        client, &BaseClient::requestFailed, this,
        [this](const RequestID &id, const QString &) { m_loops.remove(id); });
    connect(
        client, &BaseClient::requestFinalized, this,
        [this](const RequestID &id, const CompletionInfo &) { m_loops.remove(id); });
}

int ToolLoopRunner::maxRounds() const noexcept
{
    return m_maxRounds;
}

void ToolLoopRunner::setMaxRounds(int limit) noexcept
{
    m_maxRounds = limit > 0 ? limit : 1;
}

int ToolLoopRunner::rounds(const QString &requestId) const
{
    return m_loops.value(requestId).rounds;
}

void ToolLoopRunner::handleToolsCompleted(
    const QString &requestId, const QHash<QString, ToolResult> &toolResults)
{
    auto &loop = m_loops[requestId];
    if (++loop.rounds > m_maxRounds) {
        qCWarning(llmQoreLog).noquote()
            << QString("Tool continuation limit reached for request %1").arg(requestId);
        m_loops.remove(requestId);
        m_client->abortRequest(requestId, QStringLiteral("Tool continuation limit reached"));
        return;
    }

    const QJsonObject payload = m_client->buildReplayContinuation(requestId, toolResults);
    if (payload.isEmpty()) {
        qCWarning(llmQoreLog).noquote()
            << QString("Missing data for continuation request %1").arg(requestId);
        m_loops.remove(requestId);
        m_client->abortRequest(requestId, QStringLiteral("Missing data for tool continuation"));
        return;
    }

    m_client->continueRequest(requestId, payload);
}

} // namespace LLMQore
