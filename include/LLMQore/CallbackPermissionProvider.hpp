// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <utility>

#include <QPromise>

#include <LLMQore/AcpPermissionProvider.hpp>
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
        QPromise<RequestPermissionResult> promise;
        promise.start();
        if (m_callback)
            promise.addResult(m_callback(sessionId, toolCall, options));
        else
            promise.addResult(RequestPermissionResult::cancelled());
        promise.finish();
        return promise.future();
    }

private:
    Callback m_callback;
};

} // namespace LLMQore::Acp
