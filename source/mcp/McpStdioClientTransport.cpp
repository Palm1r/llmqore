// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpStdioTransport.hpp>

#include <LLMCore/Log.hpp>

#include "McpLineFramer.hpp"

#include <QFileInfo>
#include <QJsonDocument>

namespace LLMCore::Mcp {

struct McpStdioClientTransport::Impl
{
    McpLineFramer stdoutFramer;
    QByteArray stderrBuffer;
};

McpStdioClientTransport::McpStdioClientTransport(StdioLaunchConfig config, QObject *parent)
    : McpTransport(parent)
    , m_config(std::move(config))
    , m_impl(std::make_unique<Impl>())
{}

McpStdioClientTransport::~McpStdioClientTransport()
{
    stop();
}

void McpStdioClientTransport::start()
{
    if (m_process)
        return;

    m_process = new QProcess(this);
    m_process->setProcessEnvironment(m_config.environment);
    if (!m_config.workingDirectory.isEmpty())
        m_process->setWorkingDirectory(m_config.workingDirectory);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &McpStdioClientTransport::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &McpStdioClientTransport::onReadyReadStderr);
    connect(m_process, &QProcess::errorOccurred, this, &McpStdioClientTransport::onProcessErrorOccurred);
    connect(
        m_process,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this,
        &McpStdioClientTransport::onProcessFinished);

    setState(State::Connecting);

    QString program = m_config.program;
    QStringList arguments = m_config.arguments;

#ifdef Q_OS_WIN
    // On Windows, batch files (.cmd/.bat) cannot be launched directly by QProcess.
    // Wrap them via cmd.exe /c when the program name ends with such an extension,
    // OR when the program name is a well-known wrapper (npx, npm, pnpm, yarn, uvx)
    // that is usually installed as a .cmd shim.
    const QString lower = program.toLower();
    const bool isBatchExt = lower.endsWith(".cmd") || lower.endsWith(".bat");
    const bool isKnownWrapper
        = (lower == "npx" || lower == "npm" || lower == "pnpm" || lower == "yarn"
           || lower == "uvx");
    if (isBatchExt || isKnownWrapper) {
        QStringList wrapped;
        wrapped << QStringLiteral("/c") << program;
        wrapped.append(arguments);
        program = QStringLiteral("cmd.exe");
        arguments = wrapped;
    }
#endif

    qCInfo(llmMcpLog).noquote()
        << QString("Starting MCP server: %1 %2").arg(program, arguments.join(' '));
    m_process->start(program, arguments);

    if (!m_process->waitForStarted(m_config.startupTimeoutMs)) {
        const QString reason = QString("Failed to start '%1': %2").arg(program, m_process->errorString());
        qCWarning(llmMcpLog).noquote() << reason;
        setState(State::Failed);
        emit errorOccurred(reason);
        emit closed();
        return;
    }

    setState(State::Connected);
}

void McpStdioClientTransport::stop()
{
    if (!m_process)
        return;

    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(m_config.gracefulStopTimeoutMs)) {
            m_process->kill();
            m_process->waitForFinished(m_config.killTimeoutMs);
        }
    }
    m_process->deleteLater();
    m_process = nullptr;
    m_impl->stdoutFramer.clear();
    m_impl->stderrBuffer.clear();

    setState(State::Disconnected);
    emit closed();
}

bool McpStdioClientTransport::isOpen() const
{
    return m_process && m_process->state() == QProcess::Running
        && state() == State::Connected;
}

void McpStdioClientTransport::send(const QJsonObject &message)
{
    if (!isOpen()) {
        emit errorOccurred(QStringLiteral("Transport not open"));
        return;
    }
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    m_process->write(payload);
    m_process->write("\n", 1);
}

void McpStdioClientTransport::onReadyReadStdout()
{
    if (!m_process)
        return;
    const QByteArray data = m_process->readAllStandardOutput();
    const QByteArrayList lines = m_impl->stdoutFramer.append(data);
    for (const QByteArray &line : lines) {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(llmMcpLog).noquote()
                << QString("Dropping invalid JSON line: %1").arg(QString::fromUtf8(line));
            continue;
        }
        emit messageReceived(doc.object());
    }
}

void McpStdioClientTransport::onReadyReadStderr()
{
    if (!m_process)
        return;
    m_impl->stderrBuffer.append(m_process->readAllStandardError());
    int nl;
    while ((nl = m_impl->stderrBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_impl->stderrBuffer.left(nl);
        m_impl->stderrBuffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        const QString text = QString::fromUtf8(line);
        qCInfo(llmMcpLog).noquote() << "stderr:" << text;
        emit stderrLine(text);
    }
}

void McpStdioClientTransport::onProcessErrorOccurred(QProcess::ProcessError error)
{
    const QString reason = QString("QProcess error %1: %2")
                               .arg(static_cast<int>(error))
                               .arg(m_process ? m_process->errorString() : QString());
    qCWarning(llmMcpLog).noquote() << reason;
    emit errorOccurred(reason);
}

void McpStdioClientTransport::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    qCInfo(llmMcpLog).noquote() << QString("MCP server exited code=%1 status=%2")
                                       .arg(exitCode)
                                       .arg(static_cast<int>(status));
    setState(State::Disconnected);
    emit closed();
}

} // namespace LLMCore::Mcp
