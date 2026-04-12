// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTypes.hpp>

namespace LLMCore::Mcp {

class LLMCORE_EXPORT BaseResourceProvider : public QObject
{
    Q_OBJECT
public:
    explicit BaseResourceProvider(QObject *parent = nullptr);
    ~BaseResourceProvider() override = default;

    virtual QFuture<QList<ResourceInfo>> listResources() = 0;
    virtual QFuture<ResourceContents> readResource(const QString &uri) = 0;

    virtual QFuture<QList<ResourceTemplate>> listResourceTemplates();

    virtual QFuture<CompletionResult> completeArgument(
        const QString &templateUri,
        const QString &placeholderName,
        const QString &partialValue,
        const QJsonObject &contextArguments);

    virtual bool supportsSubscription() const { return false; }
    virtual void subscribe(const QString & /*uri*/) {}
    virtual void unsubscribe(const QString & /*uri*/) {}

signals:
    void listChanged();
    void resourceUpdated(const QString &uri);
};

} // namespace LLMCore::Mcp
