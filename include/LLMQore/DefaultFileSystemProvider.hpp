// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/AcpFileSystemProvider.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Acp {

class LLMQORE_EXPORT DefaultFileSystemProvider : public AcpFileSystemProvider
{
    Q_OBJECT
public:
    explicit DefaultFileSystemProvider(QObject *parent = nullptr);

    void setWritable(bool writable) { m_writable = writable; }
    bool supportsWrite() const override { return m_writable; }

    QFuture<QString> readTextFile(
        const QString &sessionId,
        const QString &path,
        std::optional<int> line,
        std::optional<int> limit) override;

    QFuture<void> writeTextFile(
        const QString &sessionId, const QString &path, const QString &content) override;

private:
    bool m_writable = true;
};

} // namespace LLMQore::Acp
