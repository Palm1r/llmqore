// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/DirectoryRootsProvider.hpp>

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore::Mcp {

namespace {

bool isUsableRoot(const QString &path)
{
    if (!path.isEmpty() && QDir::isAbsolutePath(path))
        return true;
    qCWarning(llmMcpLog).noquote()
        << QString("Ignoring root path '%1': roots must be absolute directories").arg(path);
    return false;
}

Root rootFor(const QString &path, const QString &name)
{
    const QString absolute = QDir(path).absolutePath();
    return Root{
        QUrl::fromLocalFile(absolute).toString(QUrl::FullyEncoded),
        name.isEmpty() ? QFileInfo(absolute).fileName() : name};
}

} // namespace

DirectoryRootsProvider::DirectoryRootsProvider(QObject *parent)
    : BaseRootsProvider(parent)
{}

DirectoryRootsProvider::DirectoryRootsProvider(const QStringList &paths, QObject *parent)
    : BaseRootsProvider(parent)
{
    setPaths(paths);
}

void DirectoryRootsProvider::setPaths(const QStringList &paths)
{
    QList<Root> roots;
    roots.reserve(paths.size());
    for (const QString &path : paths) {
        if (isUsableRoot(path))
            roots.append(rootFor(path, {}));
    }

    if (roots.size() == m_roots.size()) {
        bool same = true;
        for (int i = 0; i < roots.size() && same; ++i)
            same = roots[i].uri == m_roots[i].uri && roots[i].name == m_roots[i].name;
        if (same)
            return;
    }

    m_roots = roots;
    emit listChanged();
}

QStringList DirectoryRootsProvider::paths() const
{
    QStringList paths;
    paths.reserve(m_roots.size());
    for (const Root &root : m_roots)
        paths.append(QUrl(root.uri).toLocalFile());
    return paths;
}

void DirectoryRootsProvider::addPath(const QString &path, const QString &name)
{
    if (!isUsableRoot(path))
        return;

    const Root root = rootFor(path, name);
    for (const Root &existing : m_roots) {
        if (existing.uri == root.uri)
            return;
    }

    m_roots.append(root);
    emit listChanged();
}

void DirectoryRootsProvider::removePath(const QString &path)
{
    if (!isUsableRoot(path))
        return;

    const QString uri = rootFor(path, {}).uri;

    QList<Root> kept;
    kept.reserve(m_roots.size());
    for (const Root &root : m_roots) {
        if (root.uri != uri)
            kept.append(root);
    }

    if (kept.size() == m_roots.size())
        return;

    m_roots = kept;
    emit listChanged();
}

QFuture<QList<Root>> DirectoryRootsProvider::listRoots()
{
    return readyFuture(m_roots);
}

} // namespace LLMQore::Mcp
