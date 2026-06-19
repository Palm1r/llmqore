// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QFuture>
#include <QObject>
#include <QString>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Acp {

class LLMQORE_EXPORT AcpFileSystemProvider : public QObject
{
    Q_OBJECT
public:
    explicit AcpFileSystemProvider(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~AcpFileSystemProvider() override = default;

    virtual bool supportsWrite() const { return true; }

    virtual QFuture<QString> readTextFile(
        const QString &sessionId,
        const QString &path,
        std::optional<int> line,
        std::optional<int> limit) = 0;

    virtual QFuture<void> writeTextFile(
        const QString &sessionId, const QString &path, const QString &content) = 0;
};

} // namespace LLMQore::Acp
