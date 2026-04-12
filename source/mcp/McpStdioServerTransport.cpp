// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpStdioServerTransport.hpp>

#include <LLMCore/Log.hpp>

#include "McpLineFramer.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>

#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace LLMCore::Mcp {

namespace {

class StdioTrace
{
public:
    static StdioTrace &instance()
    {
        static StdioTrace inst;
        return inst;
    }

    bool isEnabled() const { return m_file.isOpen(); }

    void log(const QString &direction, const QByteArray &payload)
    {
        if (!m_file.isOpen())
            return;
        QMutexLocker locker(&m_mutex);
        const QByteArray prefix
            = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ss.zzz").toUtf8() + " ["
              + direction.toUtf8() + "] ";
        m_file.write(prefix);
        m_file.write(payload);
        if (!payload.endsWith('\n'))
            m_file.write("\n", 1);
        m_file.flush();
    }

    void note(const QString &message)
    {
        log(QStringLiteral("NOTE"), message.toUtf8());
    }

private:
    StdioTrace()
    {
        const QByteArray path = qgetenv("LLMCORE_MCP_TRACE");
        if (path.isEmpty())
            return;
        m_file.setFileName(QString::fromLocal8Bit(path));
        if (!m_file.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }
        note(QStringLiteral("--- stdio server transport trace opened ---"));
    }

    QFile m_file;
    QMutex m_mutex;
};

class StdinReaderThread : public QThread
{
public:
    explicit StdinReaderThread(McpStdioServerTransport *owner)
        : m_owner(owner)
    {}

    void stopReading()
    {
        m_stop.storeRelease(1);
        QMutexLocker lock(&m_ownerMutex);
        m_owner.clear();
    }

protected:
    void run() override
    {
        StdioTrace::instance().note(QStringLiteral("reader thread started"));
        McpLineFramer framer;
        char buf[4096];
#ifdef Q_OS_WIN
        const int fd = _fileno(stdin);
#else
        const int fd = fileno(stdin);
#endif
        while (m_stop.loadAcquire() == 0) {
#ifdef Q_OS_WIN
            const int n = _read(fd, buf, static_cast<unsigned>(sizeof(buf)));
#else
            const ssize_t n = ::read(fd, buf, sizeof(buf));
#endif
            if (n == 0) {
                StdioTrace::instance().note(QStringLiteral("stdin EOF"));
                break;
            }
            if (n < 0) {
                StdioTrace::instance().note(QStringLiteral("stdin read error"));
                break;
            }
            StdioTrace::instance().log(
                QStringLiteral("RAW<"), QByteArray(buf, static_cast<int>(n)));
            const QByteArrayList frames = framer.append(QByteArray(buf, static_cast<int>(n)));
            for (const QByteArray &frame : frames) {
                StdioTrace::instance().log(QStringLiteral("IN <"), frame);
                QJsonParseError err{};
                const QJsonDocument doc = QJsonDocument::fromJson(frame, &err);
                if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                    StdioTrace::instance().note(
                        QString("PARSE FAIL: %1").arg(err.errorString()));
                    qCWarning(llmMcpLog).noquote()
                        << QString("Dropping invalid stdin JSON line: %1")
                               .arg(QString::fromUtf8(frame));
                    continue;
                }
                QJsonObject obj = doc.object();
                postToOwner("messageReceived", Q_ARG(QJsonObject, obj));
                if (m_stop.loadAcquire() != 0)
                    break;
            }
        }
        StdioTrace::instance().note(QStringLiteral("reader thread exiting"));
        postToOwner("closed");
    }

private:
    template<typename... Args>
    void postToOwner(const char *method, Args &&...args)
    {
        QMutexLocker lock(&m_ownerMutex);
        if (!m_owner)
            return;
        QMetaObject::invokeMethod(
            m_owner.data(), method, Qt::QueuedConnection, std::forward<Args>(args)...);
    }

    QPointer<McpStdioServerTransport> m_owner;
    QMutex m_ownerMutex;
    QAtomicInt m_stop{0};
};

} // namespace

struct McpStdioServerTransport::Impl
{
    bool open = false;
    QMutex writeMutex;
    StdinReaderThread *reader = nullptr;
};

McpStdioServerTransport::McpStdioServerTransport(QObject *parent)
    : McpTransport(parent)
    , m_impl(std::make_unique<Impl>())
{}

McpStdioServerTransport::~McpStdioServerTransport()
{
    stop();
}

void McpStdioServerTransport::start()
{
    if (m_impl->open)
        return;

#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stdin, nullptr, _IONBF, 0);

    StdioTrace::instance().note(QStringLiteral("McpStdioServerTransport::start"));

    m_impl->open = true;
    setState(State::Connected);

    m_impl->reader = new StdinReaderThread(this);
    m_impl->reader->start();
}

void McpStdioServerTransport::stop()
{
    if (!m_impl->open)
        return;
    m_impl->open = false;
    if (m_impl->reader) {
        m_impl->reader->stopReading();
        if (m_impl->reader->isRunning()) {
            m_impl->reader->requestInterruption();
            m_impl->reader->wait(200);
        }
        if (!m_impl->reader->isRunning()) {
            m_impl->reader->deleteLater();
        }
        m_impl->reader = nullptr;
    }
    setState(State::Disconnected);
    emit closed();
}

bool McpStdioServerTransport::isOpen() const
{
    return m_impl->open;
}

void McpStdioServerTransport::send(const QJsonObject &message)
{
    if (!m_impl->open)
        return;
    QMutexLocker locker(&m_impl->writeMutex);
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    StdioTrace::instance().log(QStringLiteral("OUT>"), payload);
    std::fwrite(payload.constData(), 1, static_cast<size_t>(payload.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

} // namespace LLMCore::Mcp
