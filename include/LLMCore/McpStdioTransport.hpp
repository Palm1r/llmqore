// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTransport.hpp>

namespace LLMCore::Mcp {

struct LLMCORE_EXPORT StdioLaunchConfig
{
    QString program;
    QStringList arguments;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QString workingDirectory;
    int startupTimeoutMs = 10000;
    int gracefulStopTimeoutMs = 500;
    int killTimeoutMs = 1000;
};

class LLMCORE_EXPORT McpStdioClientTransport : public McpTransport
{
    Q_OBJECT
public:
    explicit McpStdioClientTransport(StdioLaunchConfig config, QObject *parent = nullptr);
    ~McpStdioClientTransport() override;

    void start() override;
    void stop() override;
    bool isOpen() const override;
    void send(const QJsonObject &message) override;

    QProcess *process() const { return m_process; }

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    StdioLaunchConfig m_config;
    QProcess *m_process = nullptr;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMCore::Mcp
