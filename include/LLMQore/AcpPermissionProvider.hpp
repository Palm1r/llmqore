// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <LLMQore/AcpTypes.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Acp {

class LLMQORE_EXPORT AcpPermissionProvider : public QObject
{
    Q_OBJECT
public:
    explicit AcpPermissionProvider(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~AcpPermissionProvider() override = default;

    virtual QFuture<RequestPermissionResult> requestPermission(
        const QString &sessionId,
        const ToolCall &toolCall,
        const QList<PermissionOption> &options) = 0;
};

} // namespace LLMQore::Acp
