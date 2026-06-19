// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/AcpAgentConfig.hpp>

#include <QJsonArray>
#include <QProcessEnvironment>

namespace LLMQore::Acp {

Rpc::StdioLaunchConfig AcpAgentConfig::toLaunchConfig() const
{
    Rpc::StdioLaunchConfig config;
    config.program = command;
    config.arguments = args;
    config.workingDirectory = cwd;
    config.startupTimeoutMs = startupTimeoutMs;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const EnvVariable &e : env)
        environment.insert(e.name, e.value);
    config.environment = environment;

    return config;
}

Rpc::StdioClientTransport *AcpAgentConfig::createTransport(QObject *parent) const
{
    return new Rpc::StdioClientTransport(toLaunchConfig(), parent);
}

QJsonObject AcpAgentConfig::toJson() const
{
    QJsonObject o;
    o.insert("command", command);

    QJsonArray a;
    for (const QString &arg : args)
        a.append(arg);
    o.insert("args", a);

    if (!env.isEmpty()) {
        QJsonObject e;
        for (const EnvVariable &v : env)
            e.insert(v.name, v.value);
        o.insert("env", e);
    }
    if (!cwd.isEmpty())
        o.insert("cwd", cwd);
    return o;
}

AcpAgentConfig AcpAgentConfig::fromJson(const QJsonObject &obj)
{
    AcpAgentConfig c;
    c.command = obj.value("command").toString();
    for (const QJsonValue &v : obj.value("args").toArray())
        c.args.append(v.toString());
    const QJsonObject e = obj.value("env").toObject();
    for (auto it = e.constBegin(); it != e.constEnd(); ++it)
        c.env.append(EnvVariable{it.key(), it.value().toString()});
    c.cwd = obj.value("cwd").toString();
    if (obj.contains("startupTimeoutMs"))
        c.startupTimeoutMs = obj.value("startupTimeoutMs").toInt(c.startupTimeoutMs);
    return c;
}

} // namespace LLMQore::Acp
