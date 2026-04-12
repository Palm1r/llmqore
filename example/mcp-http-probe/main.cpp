// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT
//
// Smoke test for McpHttpTransport. Connects to an MCP server at the given
// URL, performs the initialize handshake, lists tools, optionally calls one,
// and exits.
//
// Usage:
//   mcp-http-probe --spec <date> <url> [tool-to-call]
//
// Supported spec revisions:
//   2024-11-05, 2025-03-26 (default)
//
// Example (Qt Creator):
//   mcp-http-probe --spec 2024-11-05 http://127.0.0.1:3001/sse list_sessions

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QtDebug>

#include <LLMCore/Mcp>

using namespace LLMCore::Mcp;

namespace {

McpClient *g_client = nullptr;
int g_exitCode = 0;

void finish(int code)
{
    g_exitCode = code;
    if (g_client)
        g_client->shutdown();
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void die(const QString &msg, int code = 1)
{
    qCritical().noquote() << msg;
    finish(code);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("mcp-http-probe");

    QCommandLineParser parser;
    parser.setApplicationDescription("Smoke-test LLMCore's McpHttpTransport.");
    parser.addHelpOption();
    QCommandLineOption specOpt(
        "spec",
        "MCP spec revision to speak. Supported: 2024-11-05, 2025-03-26, "
        "2025-06-18, 2025-11-25 (default, 'latest' alias).",
        "date",
        "2025-11-25");
    parser.addOption(specOpt);
    parser.addPositionalArgument("url", "MCP server URL");
    parser.addPositionalArgument("tool", "Optional tool name to call", "[tool]");
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        parser.showHelp(1);

    const QString specStr = parser.value(specOpt);
    HttpTransportConfig cfg;
    cfg.endpoint = QUrl(positional.first());
    if (specStr == QLatin1String("2024-11-05")) {
        cfg.spec = McpHttpSpec::V2024_11_05;
    } else if (specStr == QLatin1String("2025-03-26")
               || specStr == QLatin1String("2025-06-18")
               || specStr == QLatin1String("2025-11-25")
               || specStr == QLatin1String("latest")) {
        cfg.spec = McpHttpSpec::V2025_03_26;
    } else {
        qCritical().noquote() << "Unknown --spec value:" << specStr;
        return 1;
    }

    const QString toolToCall = positional.size() > 1 ? positional.at(1) : QString();

    qInfo().noquote() << "Endpoint:" << cfg.endpoint.toString();
    qInfo().noquote() << "Spec:   " << specStr;

    auto *transport = new McpHttpTransport(cfg, nullptr, &app);
    auto *client = new McpClient(transport, Implementation{"mcp-http-probe", "0.1.0"}, &app);
    g_client = client;

    QObject::connect(client, &McpClient::errorOccurred, &app, [](const QString &err) {
        die(QString("client error: %1").arg(err), 2);
    });
    QObject::connect(transport, &McpTransport::errorOccurred, &app, [](const QString &err) {
        qWarning().noquote() << "transport error:" << err;
    });

    QTimer::singleShot(20000, &app, []() { die("timeout (20s)", 3); });

    auto initFuture = client->connectAndInitialize(std::chrono::seconds(10));
    auto *initWatcher = new QFutureWatcher<InitializeResult>(&app);
    QObject::connect(initWatcher, &QFutureWatcher<InitializeResult>::finished, &app, [=, &app]() {
        try {
            const InitializeResult result = initWatcher->result();
            qInfo().noquote() << "=== initialize ok ===";
            qInfo().noquote() << "  server:  " << result.serverInfo.name
                              << result.serverInfo.version;
            qInfo().noquote() << "  version: " << result.protocolVersion;
            if (result.capabilities.tools)
                qInfo().noquote() << "  tools.listChanged:"
                                  << result.capabilities.tools->listChanged;
            if (result.capabilities.resources)
                qInfo().noquote() << "  resources.subscribe:"
                                  << result.capabilities.resources->subscribe;
        } catch (const std::exception &e) {
            die(QString("initialize failed: %1").arg(e.what()));
            return;
        }
        initWatcher->deleteLater();

        auto listFuture = client->listTools();
        auto *listWatcher = new QFutureWatcher<QList<ToolInfo>>(&app);
        QObject::connect(
            listWatcher, &QFutureWatcher<QList<ToolInfo>>::finished, &app, [=, &app]() {
                try {
                    const QList<ToolInfo> tools = listWatcher->result();
                    qInfo().noquote() << "=== tools/list ok ===";
                    for (const ToolInfo &t : tools) {
                        qInfo().noquote()
                            << "  -" << t.name << ":" << t.description.left(70);
                    }
                } catch (const std::exception &e) {
                    die(QString("tools/list failed: %1").arg(e.what()));
                    return;
                }
                listWatcher->deleteLater();

                if (toolToCall.isEmpty()) {
                    finish(0);
                    return;
                }

                qInfo().noquote() << QString("=== calling %1 ===").arg(toolToCall);
                auto callFuture = client->callTool(toolToCall, QJsonObject{});
                auto *callWatcher = new QFutureWatcher<LLMCore::ToolResult>(&app);
                QObject::connect(
                    callWatcher,
                    &QFutureWatcher<LLMCore::ToolResult>::finished,
                    &app,
                    [=]() {
                        try {
                            const LLMCore::ToolResult r = callWatcher->result();
                            qInfo().noquote()
                                << "  isError:" << r.isError
                                << "content blocks:" << r.content.size();
                            qInfo().noquote() << "  raw envelope:"
                                              << QString::fromUtf8(
                                                     QJsonDocument(r.toJson())
                                                         .toJson(QJsonDocument::Indented));
                            qInfo().noquote() << "  asText:" << r.asText();
                        } catch (const std::exception &e) {
                            die(QString("tool call failed: %1").arg(e.what()));
                            return;
                        }
                        callWatcher->deleteLater();
                        finish(0);
                    });
                callWatcher->setFuture(callFuture);
            });
        listWatcher->setFuture(listFuture);
    });
    initWatcher->setFuture(initFuture);

    app.exec();
    return g_exitCode;
}
