// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/BaseResourceProvider.hpp>

#include <QPromise>

namespace LLMCore::Mcp {

BaseResourceProvider::BaseResourceProvider(QObject *parent)
    : QObject(parent)
{}

QFuture<QList<ResourceTemplate>> BaseResourceProvider::listResourceTemplates()
{
    auto promise = std::make_shared<QPromise<QList<ResourceTemplate>>>();
    promise->start();
    promise->addResult(QList<ResourceTemplate>{});
    promise->finish();
    return promise->future();
}

QFuture<CompletionResult> BaseResourceProvider::completeArgument(
    const QString & /*templateUri*/,
    const QString & /*placeholderName*/,
    const QString & /*partialValue*/,
    const QJsonObject & /*contextArguments*/)
{
    auto promise = std::make_shared<QPromise<CompletionResult>>();
    promise->start();
    promise->addResult(CompletionResult{});
    promise->finish();
    return promise->future();
}

} // namespace LLMCore::Mcp
