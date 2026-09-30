// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/StaticResourceProvider.hpp>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/RpcExceptions.hpp>

namespace LLMQore::Mcp {

StaticResourceProvider::StaticResourceProvider(QObject *parent)
    : BaseResourceProvider(parent)
{}

void StaticResourceProvider::put(Entry entry)
{
    const QString uri = entry.info.uri;
    const auto existing = m_entries.constFind(uri);
    const bool isNew = existing == m_entries.constEnd();
    const bool listed = isNew || existing->info.toJson() != entry.info.toJson();
    if (isNew)
        m_order.append(uri);

    m_entries.insert(uri, std::move(entry));
    if (listed)
        emit listChanged();
}

void StaticResourceProvider::addText(
    const QString &uri, const QString &text, const QString &name, const QString &mimeType)
{
    Entry entry;
    entry.info.uri = uri;
    entry.info.name = name.isEmpty() ? uri : name;
    entry.info.mimeType = mimeType;
    entry.contents.uri = uri;
    entry.contents.mimeType = mimeType;
    entry.contents.text = text;

    put(std::move(entry));
}

void StaticResourceProvider::addBlob(
    const QString &uri, const QByteArray &blob, const QString &name, const QString &mimeType)
{
    Entry entry;
    entry.info.uri = uri;
    entry.info.name = name.isEmpty() ? uri : name;
    entry.info.mimeType = mimeType;
    entry.contents.uri = uri;
    entry.contents.mimeType = mimeType;
    entry.contents.blob = blob;

    put(std::move(entry));
}

void StaticResourceProvider::remove(const QString &uri)
{
    if (m_entries.remove(uri) == 0)
        return;

    m_order.removeAll(uri);
    emit listChanged();
}

void StaticResourceProvider::clear()
{
    if (m_entries.isEmpty())
        return;

    m_entries.clear();
    m_order.clear();
    emit listChanged();
}

bool StaticResourceProvider::contains(const QString &uri) const
{
    return m_entries.contains(uri);
}

int StaticResourceProvider::count() const
{
    return static_cast<int>(m_entries.size());
}

QFuture<QList<ResourceInfo>> StaticResourceProvider::listResources()
{
    QList<ResourceInfo> resources;
    resources.reserve(m_order.size());
    for (const QString &uri : m_order)
        resources.append(m_entries.value(uri).info);

    return readyFuture(resources);
}

QFuture<ResourceContents> StaticResourceProvider::readResource(const QString &uri)
{
    const auto entry = m_entries.constFind(uri);
    if (entry == m_entries.constEnd()) {
        return failedFuture<ResourceContents>(
            Rpc::ProtocolError(QString("Resource not found: %1").arg(uri)));
    }

    return readyFuture(entry->contents);
}

} // namespace LLMQore::Mcp
