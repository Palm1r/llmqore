// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace McpBridge {

enum class UpstreamType {
    Stdio,
    Sse, // legacy SSE / Streamable HTTP (MCP over HTTP)
};

struct UpstreamEntry
{
    QString name;
    bool enabled = true;
    UpstreamType type = UpstreamType::Stdio;

    // stdio
    QString command;
    QStringList args;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // sse / http
    QUrl url;
    QHash<QString, QString> headers;
};

struct BridgeConfig
{
    quint16 port = 8808;
    QHostAddress address = QHostAddress::LocalHost;
    bool stdioMode = false;
    QList<UpstreamEntry> upstreams;
};

BridgeConfig loadConfig(const QString &path);

} // namespace McpBridge
