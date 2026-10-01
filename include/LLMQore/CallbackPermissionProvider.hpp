// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <utility>

#include <LLMQore/AcpPermissionProvider.hpp>
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Acp {

class LLMQORE_EXPORT CallbackPermissionProvider : public AcpPermissionProvider
{
    Q_OBJECT
public:
    using Callback = std::function<RequestPermissionResult(
        const QString &sessionId,
        const ToolCall &toolCall,
        const QList<PermissionOption> &options)>;

    explicit CallbackPermissionProvider(Callback callback, QObject *parent = nullptr)
        : AcpPermissionProvider(parent)
        , m_callback(std::move(callback))
    {}

    QFuture<RequestPermissionResult> requestPermission(
        const QString &sessionId,
        const ToolCall &toolCall,
        const QList<PermissionOption> &options) override
    {
        if (!m_callback)
            return readyFuture(RequestPermissionResult::cancelled());
        return readyFuture(m_callback(sessionId, toolCall, options));
    }

private:
    Callback m_callback;
};

} // namespace LLMQore::Acp
