// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/RpcTransport.hpp>

namespace LLMQore::Rpc {

struct LLMQORE_EXPORT StdioLaunchConfig
{
    QString program;
    QStringList arguments;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QString workingDirectory;
    int startupTimeoutMs = 10000;
    int gracefulStopTimeoutMs = 500;
    int killTimeoutMs = 1000;
};

class LLMQORE_EXPORT StdioClientTransport : public Transport
{
    Q_OBJECT
public:
    explicit StdioClientTransport(Rpc::StdioLaunchConfig config, QObject *parent = nullptr);
    ~StdioClientTransport() override;

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
    Rpc::StdioLaunchConfig m_config;
    QProcess *m_process = nullptr;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore::Rpc
