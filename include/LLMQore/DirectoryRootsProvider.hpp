// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>
#include <QStringList>

#include <LLMQore/BaseRootsProvider.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT DirectoryRootsProvider : public BaseRootsProvider
{
    Q_OBJECT
public:
    explicit DirectoryRootsProvider(QObject *parent = nullptr);
    explicit DirectoryRootsProvider(const QStringList &paths, QObject *parent = nullptr);

    void setPaths(const QStringList &paths);
    [[nodiscard]] QStringList paths() const;

    void addPath(const QString &path, const QString &name = {});
    void removePath(const QString &path);

    QFuture<QList<Root>> listRoots() override;

private:
    QList<Root> m_roots;
};

} // namespace LLMQore::Mcp
