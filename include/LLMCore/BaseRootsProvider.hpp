// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QList>
#include <QObject>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTypes.hpp>

namespace LLMCore::Mcp {

class LLMCORE_EXPORT BaseRootsProvider : public QObject
{
    Q_OBJECT
public:
    explicit BaseRootsProvider(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~BaseRootsProvider() override = default;

    virtual QFuture<QList<Root>> listRoots() = 0;

signals:
    void listChanged();
};

} // namespace LLMCore::Mcp
