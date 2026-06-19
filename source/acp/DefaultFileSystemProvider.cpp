// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/DefaultFileSystemProvider.hpp>

#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPromise>
#include <QStringList>

#include <LLMQore/RpcExceptions.hpp>

namespace LLMQore::Acp {

namespace {

template<typename T>
QFuture<T> resolved(T value)
{
    QPromise<T> p;
    p.start();
    p.addResult(std::move(value));
    p.finish();
    return p.future();
}

QFuture<void> resolvedVoid()
{
    QPromise<void> p;
    p.start();
    p.finish();
    return p.future();
}

template<typename T>
QFuture<T> rejected(int code, const QString &message)
{
    QPromise<T> p;
    p.start();
    p.setException(std::make_exception_ptr(Rpc::RemoteError(code, message)));
    p.finish();
    return p.future();
}

QFuture<void> rejectedVoid(int code, const QString &message)
{
    QPromise<void> p;
    p.start();
    p.setException(std::make_exception_ptr(Rpc::RemoteError(code, message)));
    p.finish();
    return p.future();
}

// Apply 1-based `line` start and `limit` line count to text.
QString sliceLines(const QString &text, std::optional<int> line, std::optional<int> limit)
{
    if (!line && !limit)
        return text;

    const QStringList lines = text.split(QLatin1Char('\n'));
    int start = line ? qMax(0, *line - 1) : 0;
    if (start >= lines.size())
        return QString();
    int count = limit ? qMax(0, *limit) : (lines.size() - start);
    count = qMin(count, lines.size() - start);
    return lines.mid(start, count).join(QLatin1Char('\n'));
}

} // namespace

DefaultFileSystemProvider::DefaultFileSystemProvider(QObject *parent)
    : AcpFileSystemProvider(parent)
{}

QFuture<QString> DefaultFileSystemProvider::readTextFile(
    const QString &, const QString &path, std::optional<int> line, std::optional<int> limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return rejected<QString>(
            Rpc::ErrorCode::InvalidParams,
            QString("Cannot open '%1': %2").arg(path, file.errorString()));
    }
    const QString content = QString::fromUtf8(file.readAll());
    return resolved<QString>(sliceLines(content, line, limit));
}

QFuture<void> DefaultFileSystemProvider::writeTextFile(
    const QString &, const QString &path, const QString &content)
{
    if (!m_writable)
        return rejectedVoid(Rpc::ErrorCode::MethodNotFound, QStringLiteral("writes disabled"));

    QFileInfo(path).absoluteDir().mkpath(QStringLiteral("."));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return rejectedVoid(
            Rpc::ErrorCode::InvalidParams,
            QString("Cannot write '%1': %2").arg(path, file.errorString()));
    }
    file.write(content.toUtf8());
    file.close();
    return resolvedVoid();
}

} // namespace LLMQore::Acp
