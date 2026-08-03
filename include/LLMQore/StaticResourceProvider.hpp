// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <LLMQore/BaseResourceProvider.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Mcp {

class LLMQORE_EXPORT StaticResourceProvider : public BaseResourceProvider
{
    Q_OBJECT
public:
    explicit StaticResourceProvider(QObject *parent = nullptr);

    void addText(
        const QString &uri,
        const QString &text,
        const QString &name = {},
        const QString &mimeType = QStringLiteral("text/plain"));

    void addBlob(
        const QString &uri,
        const QByteArray &blob,
        const QString &name = {},
        const QString &mimeType = QStringLiteral("application/octet-stream"));

    void remove(const QString &uri);
    void clear();

    [[nodiscard]] bool contains(const QString &uri) const;
    [[nodiscard]] int count() const;

    QFuture<QList<ResourceInfo>> listResources() override;
    QFuture<ResourceContents> readResource(const QString &uri) override;

private:
    struct Entry
    {
        ResourceInfo info;
        ResourceContents contents;
    };

    void put(Entry entry);

    QList<QString> m_order;
    QHash<QString, Entry> m_entries;
};

} // namespace LLMQore::Mcp
