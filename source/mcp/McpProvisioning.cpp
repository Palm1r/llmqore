// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpProvisioning.hpp>

#include <LLMQore/Log.hpp>
#include <LLMQore/RpcStdioTransport.hpp>

#include <QJsonArray>

namespace LLMQore::Mcp {

ServerEndpoint ServerEndpoint::fromJson(const QString &name, const QJsonObject &entry)
{
    ServerEndpoint endpoint;
    endpoint.name = name;

    if (entry.contains(QLatin1String("url"))) {
        endpoint.url = QUrl(entry.value(QLatin1String("url")).toString());
        endpoint.httpSpec = entry.value(QLatin1String("spec")).toString();
        if (entry.contains(QLatin1String("httpSpec"))) {
            qCWarning(llmMcpLog).noquote() << QString(
                "MCP server '%1': key 'httpSpec' is deprecated, use 'spec'").arg(name);
            if (endpoint.httpSpec.isEmpty())
                endpoint.httpSpec = entry.value(QLatin1String("httpSpec")).toString();
        }
        const QJsonObject headers = entry.value(QLatin1String("headers")).toObject();
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            if (!it.value().isString()) {
                qCWarning(llmMcpLog).noquote() << QString(
                    "MCP server '%1': skipping non-string header '%2'").arg(name, it.key());
                continue;
            }
            endpoint.headers.insert(it.key(), it.value().toString());
        }
    } else {
        endpoint.command = entry.value(QLatin1String("command")).toString();
        const QJsonArray args = entry.value(QLatin1String("args")).toArray();
        for (const QJsonValue &arg : args) {
            if (!arg.isString()) {
                qCWarning(llmMcpLog).noquote() << QString(
                    "MCP server '%1': skipping non-string args entry").arg(name);
                continue;
            }
            endpoint.arguments.append(arg.toString());
        }
        const QJsonObject envObj = entry.value(QLatin1String("env")).toObject();
        for (auto it = envObj.begin(); it != envObj.end(); ++it) {
            if (!it.value().isString()) {
                qCWarning(llmMcpLog).noquote() << QString(
                    "MCP server '%1': skipping non-string env value '%2'").arg(name, it.key());
                continue;
            }
            endpoint.env.insert(it.key(), it.value().toString());
        }
        endpoint.workingDirectory = entry.value(QLatin1String("workingDirectory")).toString();
    }

    return endpoint;
}

QList<ServerEndpoint> parseServerMap(const QJsonObject &config)
{
    QList<ServerEndpoint> endpoints;
    const QJsonObject servers = config.value(QLatin1String("mcpServers")).toObject();
    for (auto it = servers.begin(); it != servers.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        if (!entry.value(QLatin1String("enable")).toBool(true)) {
            qCInfo(llmMcpLog).noquote()
                << QString("Skipping disabled MCP server '%1'").arg(it.key());
            continue;
        }
        ServerEndpoint endpoint = ServerEndpoint::fromJson(it.key(), entry);
        if (!endpoint.isValid()) {
            qCWarning(llmMcpLog).noquote()
                << QString("Skipping MCP server '%1': neither a usable http(s) url nor a command")
                       .arg(it.key());
            continue;
        }
        endpoints.append(std::move(endpoint));
    }
    return endpoints;
}

McpHttpSpec parseHttpSpec(const QString &spec)
{
    if (spec.isEmpty())
        return McpHttpSpec::Latest;
    if (spec == QLatin1String("2024-11-05"))
        return McpHttpSpec::V2024_11_05;
    if (spec == QLatin1String("2025-03-26") || spec == QLatin1String("2025-06-18")
        || spec == QLatin1String("2025-11-25") || spec == QLatin1String("latest"))
        return McpHttpSpec::V2025_03_26;

    qCWarning(llmMcpLog).noquote()
        << QString("Unknown httpSpec '%1' -- falling back to latest.").arg(spec);
    return McpHttpSpec::Latest;
}

Rpc::Transport *makeHttpTransport(
    HttpTransportConfig config, LLMQore::HttpTransport *transport, QObject *parent)
{
    switch (config.spec) {
    case McpHttpSpec::V2024_11_05:
        return new McpSseHttpTransport(std::move(config), transport, parent);
    case McpHttpSpec::V2025_03_26:
        return new McpStreamableHttpTransport(std::move(config), transport, parent);
    }
    return nullptr;
}

Rpc::Transport *makeTransport(const ServerEndpoint &endpoint, QObject *parent)
{
    if (endpoint.hasHttpEndpoint()) {
        HttpTransportConfig cfg;
        cfg.endpoint = endpoint.url;
        cfg.headers = endpoint.headers;
        cfg.spec = parseHttpSpec(endpoint.httpSpec);
        return makeHttpTransport(std::move(cfg), nullptr, parent);
    }

    if (!endpoint.command.isEmpty()) {
        Rpc::StdioLaunchConfig cfg;
        cfg.program = endpoint.command;
        cfg.arguments = endpoint.arguments;
        cfg.environment = endpoint.env;
        cfg.workingDirectory = endpoint.workingDirectory;
        return new Rpc::StdioClientTransport(cfg, parent);
    }

    qCWarning(llmMcpLog).noquote()
        << QString("MCP server '%1': neither a usable http(s) url nor a command was given")
               .arg(endpoint.name);
    return nullptr;
}

} // namespace LLMQore::Mcp
