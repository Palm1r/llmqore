// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <LLMQore/AcpTypes.hpp>
#include <LLMQore/LLMQore_global.h>
#include <LLMQore/RpcStdioTransport.hpp>

namespace LLMQore::Acp {

struct LLMQORE_EXPORT AcpAgentConfig
{
    QString command;
    QStringList args;
    QList<EnvVariable> env; // added on top of the inherited environment
    QString cwd;
    int startupTimeoutMs = 10000;

    Rpc::StdioLaunchConfig toLaunchConfig() const;

    Rpc::StdioClientTransport *createTransport(QObject *parent = nullptr) const;

    QJsonObject toJson() const;
    static AcpAgentConfig fromJson(const QJsonObject &obj);
};

} // namespace LLMQore::Acp
