// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <utility>

#include <LLMQore/BaseElicitationProvider.hpp>
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT CallbackElicitationProvider : public BaseElicitationProvider
{
    Q_OBJECT
public:
    using Callback = std::function<ElicitResult(const ElicitRequestParams &params)>;

    explicit CallbackElicitationProvider(Callback callback, QObject *parent = nullptr)
        : BaseElicitationProvider(parent)
        , m_callback(std::move(callback))
    {}

    QFuture<ElicitResult> elicit(const ElicitRequestParams &params) override
    {
        if (!m_callback)
            return readyFuture(ElicitResult{QString::fromLatin1(ElicitAction::Cancel), {}});
        return readyFuture(m_callback(params));
    }

private:
    Callback m_callback;
};

} // namespace LLMQore::Mcp
