// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "BridgeConfig.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace McpBridge {

BridgeConfig loadConfig(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCritical().noquote() << "Cannot open config:" << path;
        return {};
    }

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qCritical().noquote() << "Config parse error:" << err.errorString();
        return {};
    }

    const QJsonObject root = doc.object();
    const QJsonObject serversObj = root["mcpServers"].toObject();
    if (serversObj.isEmpty()) {
        qCritical() << "No servers defined in mcpServers.";
        return {};
    }

    BridgeConfig cfg;
    cfg.port = static_cast<quint16>(root["port"].toInt(8808));

    if (root.contains("host"))
        cfg.address = QHostAddress(root["host"].toString());

    const QStringList names = serversObj.keys();
    for (const QString &name : names) {
        const QJsonObject entry = serversObj[name].toObject();

        UpstreamEntry upstream;
        upstream.name = name;

        // "enable" is optional; default true. Explicit false skips the entry.
        if (entry.contains("enable") && !entry["enable"].toBool(true)) {
            qInfo().noquote() << "Skipping disabled server:" << name;
            continue;
        }

        const QString typeStr = entry["type"].toString().toLower();
        // `"sse"` and `"http"` both mean «MCP over HTTP» — which wire spec
        // is actually spoken is selected by `"httpSpec"` below, not by the
        // `"type"` string. Historically we also accepted `"streamable-http"`
        // as a synonym of `"http"`; it was a pure alias and has been
        // dropped to keep the config surface honest.
        if (typeStr == "sse" || typeStr == "http") {
            upstream.type = UpstreamType::Sse;
            upstream.url = QUrl(entry["url"].toString());
            if (!upstream.url.isValid()) {
                qWarning().noquote() << "Skipping" << name << "— invalid url.";
                continue;
            }
            if (entry.contains("headers")) {
                const QJsonObject hdrs = entry["headers"].toObject();
                for (auto it = hdrs.begin(); it != hdrs.end(); ++it)
                    upstream.headers.insert(it.key(), it.value().toString());
            }

            // `"type"` selects the transport family (stdio vs HTTP). The
            // HTTP wire spec is *separately* chosen via `"httpSpec"` and
            // has to be set by the user when the default (Latest, currently
            // Streamable HTTP 2025-03-26) doesn't match the upstream.
            //
            // Concretely: servers that advertise a `/sse` URL usually speak
            // the legacy 2024-11-05 transport (split GET /sse + POST
            // /messages). For those, the user must write:
            //     "httpSpec": "2024-11-05"
            // Without it, initialize round-trips to a spec the server
            // doesn't recognise and hangs until the client timeout.
            if (entry.contains("httpSpec"))
                upstream.httpSpec = entry["httpSpec"].toString();
        } else {
            // Default: stdio. Accept explicit "stdio" too.
            upstream.type = UpstreamType::Stdio;
            upstream.command = entry["command"].toString();
            if (upstream.command.isEmpty()) {
                qWarning().noquote() << "Skipping" << name << "— no command.";
                continue;
            }

            const QJsonArray argsArray = entry["args"].toArray();
            for (const QJsonValue &v : argsArray)
                upstream.args << v.toString();

            if (entry.contains("env")) {
                const QJsonObject envObj = entry["env"].toObject();
                for (auto it = envObj.begin(); it != envObj.end(); ++it)
                    upstream.env.insert(it.key(), it.value().toString());
            }
        }

        cfg.upstreams.append(upstream);
    }

    return cfg;
}

} // namespace McpBridge
