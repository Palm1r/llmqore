// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QObject>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTypes.hpp>

namespace LLMCore::Mcp {

class LLMCORE_EXPORT BaseElicitationProvider : public QObject
{
    Q_OBJECT
public:
    explicit BaseElicitationProvider(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~BaseElicitationProvider() override = default;

    virtual QFuture<ElicitResult> elicit(const ElicitRequestParams &params) = 0;
};

} // namespace LLMCore::Mcp
