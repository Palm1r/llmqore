// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <QCoreApplication>
#include <QCommandLineParser>

#include "BridgeConfig.hpp"
#include "BridgeServer.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("mcp-bridge");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "MCP Bridge — aggregate stdio MCP servers behind a single HTTP/SSE endpoint.");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOpt("port", "HTTP port (overrides config).", "port");
    QCommandLineOption hostOpt("host", "Bind address (default 127.0.0.1).", "address");
    QCommandLineOption stdioOpt(
        "stdio",
        "Serve MCP over stdio instead of HTTP (for Claude Desktop and other stdio-only hosts).");
    parser.addOption(portOpt);
    parser.addOption(hostOpt);
    parser.addOption(stdioOpt);
    parser.addPositionalArgument("config", "Path to mcp-bridge.json", "[config]");
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    const QString configPath = positional.isEmpty() ? QStringLiteral("mcp-bridge.json")
                                                    : positional.first();

    McpBridge::BridgeConfig config = McpBridge::loadConfig(configPath);
    if (config.upstreams.isEmpty())
        return 1;

    if (parser.isSet(portOpt))
        config.port = parser.value(portOpt).toUShort();
    if (parser.isSet(hostOpt))
        config.address = QHostAddress(parser.value(hostOpt));
    if (parser.isSet(stdioOpt))
        config.stdioMode = true;

    auto *bridge = new McpBridge::BridgeServer(config, &app);

    QObject::connect(bridge, &McpBridge::BridgeServer::startFailed, &app, [](const QString &err) {
        qCritical().noquote() << err;
        QCoreApplication::exit(1);
    });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, bridge, &McpBridge::BridgeServer::shutdown);

    bridge->start();

    return app.exec();
}
