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
        if (typeStr == "sse" || typeStr == "http" || typeStr == "streamable-http") {
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
