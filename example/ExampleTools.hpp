// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QMimeType>
#include <QNetworkInterface>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrent>

#include <LLMCore/Tools>

namespace Example {

class DateTimeTool : public LLMCore::BaseTool
{
    Q_OBJECT
public:
    explicit DateTimeTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "get_datetime"; }
    QString displayName() const override { return "Date & Time"; }
    QString description() const override
    {
        return "Returns the current date and time. No parameters required.";
    }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{}},
        };
    }

    QFuture<LLMCore::ToolResult> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run([]() -> LLMCore::ToolResult {
            return LLMCore::ToolResult::text(
                QDateTime::currentDateTime().toString(Qt::ISODate));
        });
    }
};

class CalculatorTool : public LLMCore::BaseTool
{
    Q_OBJECT
public:
    explicit CalculatorTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "calculator"; }
    QString displayName() const override { return "Calculator"; }
    QString description() const override
    {
        return "Performs basic arithmetic. Parameters: 'a' (number), 'b' (number), "
               "'operation' (one of: add, subtract, multiply, divide).";
    }

    QJsonObject parametersSchema() const override
    {
        QJsonObject properties{
            {"a", QJsonObject{{"type", "number"}, {"description", "First operand"}}},
            {"b", QJsonObject{{"type", "number"}, {"description", "Second operand"}}},
            {"operation",
             QJsonObject{
                 {"type", "string"},
                 {"description", "The arithmetic operation"},
                 {"enum", QJsonArray{"add", "subtract", "multiply", "divide"}}}}};

        return QJsonObject{
            {"type", "object"},
            {"properties", properties},
            {"required", QJsonArray{"a", "b", "operation"}}};
    }

    QFuture<LLMCore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> LLMCore::ToolResult {
            double a = input.value("a").toDouble();
            double b = input.value("b").toDouble();
            QString op = input.value("operation").toString();

            double result = 0;
            if (op == "add")
                result = a + b;
            else if (op == "subtract")
                result = a - b;
            else if (op == "multiply")
                result = a * b;
            else if (op == "divide") {
                if (b == 0)
                    return LLMCore::ToolResult::error(QStringLiteral("division by zero"));
                result = a / b;
            } else {
                return LLMCore::ToolResult::error(
                    QString("unknown operation '%1'").arg(op));
            }

            return LLMCore::ToolResult::text(QString::number(result, 'g', 10));
        });
    }
};

// --- Extra tools used by the MCP server demo ---
// Kept intentionally distinct from the three "built-in" tools above so that
// the chat example shows MCP-sourced and locally-provided tools coexisting
// without id collisions.

class IPv4Tool : public LLMCore::BaseTool
{
    Q_OBJECT
public:
    explicit IPv4Tool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "get_ipv4"; }
    QString displayName() const override { return "IPv4 Addresses"; }
    QString description() const override
    {
        return "Returns all non-loopback IPv4 addresses on this machine, grouped by "
               "network interface. No parameters required.";
    }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{}},
        };
    }

    QFuture<LLMCore::ToolResult> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run([]() -> LLMCore::ToolResult {
            QJsonArray interfaces;
            const auto allIfaces = QNetworkInterface::allInterfaces();
            for (const auto &iface : allIfaces) {
                const auto flags = iface.flags();
                if (!(flags & QNetworkInterface::IsUp)
                    || !(flags & QNetworkInterface::IsRunning))
                    continue;
                if (flags & QNetworkInterface::IsLoopBack)
                    continue;

                QJsonArray addresses;
                for (const auto &entry : iface.addressEntries()) {
                    const QHostAddress addr = entry.ip();
                    if (addr.protocol() != QAbstractSocket::IPv4Protocol)
                        continue;
                    if (addr.isLoopback())
                        continue;
                    addresses.append(addr.toString());
                }
                if (addresses.isEmpty())
                    continue;

                interfaces.append(QJsonObject{
                    {"name", iface.humanReadableName()},
                    {"addresses", addresses},
                });
            }
            return LLMCore::ToolResult::text(QString::fromUtf8(
                QJsonDocument(interfaces).toJson(QJsonDocument::Compact)));
        });
    }
};

class EnvTool : public LLMCore::BaseTool
{
    Q_OBJECT
public:
    explicit EnvTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "get_env"; }
    QString displayName() const override { return "Environment Variable"; }
    QString description() const override
    {
        return "Reads the value of an environment variable. Parameters: 'name' (string).";
    }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties",
             QJsonObject{
                 {"name",
                  QJsonObject{
                      {"type", "string"}, {"description", "Environment variable name"}}}}},
            {"required", QJsonArray{"name"}},
        };
    }

    QFuture<LLMCore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> LLMCore::ToolResult {
            const QString name = input.value("name").toString();
            const QByteArray raw = qgetenv(name.toLocal8Bit().constData());
            if (raw.isNull())
                return LLMCore::ToolResult::text(QStringLiteral("(not set)"));
            return LLMCore::ToolResult::text(QString::fromLocal8Bit(raw));
        });
    }
};

// Reads an image file by absolute path and returns it as an MCP image content
// block (base64 on the wire, preserved end-to-end through McpServer /
// McpRemoteTool / ClaudeClient continuation). Intentionally exposed only
// through the MCP server demo — you would typically guard it with a
// workspace sandbox in real use; here the caller is expected to trust the
// LLM with full disk read access for the demo scenario.
class ImageReadTool : public LLMCore::BaseTool
{
    Q_OBJECT
public:
    // Hard upper bound on returned image size. 10 MiB is enough for most
    // screenshots and photos but small enough to keep base64 payloads out
    // of pathological territory on the wire.
    static constexpr qint64 kMaxImageBytes = 10 * 1024 * 1024;

    explicit ImageReadTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "read_image"; }
    QString displayName() const override { return "Read Image"; }
    QString description() const override
    {
        return "Reads an image file from disk by absolute path and returns it as "
               "a base64-encoded image content block so the model can view it. "
               "Parameters: 'path' (string, absolute file path). Max size: 10 MiB. "
               "Supported MIME types: image/png, image/jpeg, image/gif, image/webp.";
    }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties",
             QJsonObject{
                 {"path",
                  QJsonObject{
                      {"type", "string"},
                      {"description", "Absolute path to the image file on disk"}}}}},
            {"required", QJsonArray{"path"}},
        };
    }

    QFuture<LLMCore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> LLMCore::ToolResult {
            const QString path = input.value("path").toString();
            if (path.isEmpty())
                return LLMCore::ToolResult::error(QStringLiteral("'path' is required"));

            const QFileInfo info(path);
            if (!info.isAbsolute())
                return LLMCore::ToolResult::error(
                    QString("path must be absolute: %1").arg(path));
            if (!info.exists() || !info.isFile())
                return LLMCore::ToolResult::error(
                    QString("file not found: %1").arg(path));
            if (info.size() > kMaxImageBytes)
                return LLMCore::ToolResult::error(
                    QString("file too large: %1 bytes (max %2)")
                        .arg(info.size())
                        .arg(kMaxImageBytes));

            QMimeDatabase mimeDb;
            const QMimeType mime = mimeDb.mimeTypeForFile(info);
            const QString mimeName = mime.name();
            if (!mimeName.startsWith(QLatin1String("image/"))) {
                return LLMCore::ToolResult::error(
                    QString("not an image file (detected MIME: %1)").arg(mimeName));
            }

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return LLMCore::ToolResult::error(
                    QString("cannot open file: %1").arg(file.errorString()));
            }
            const QByteArray bytes = file.readAll();
            file.close();

            LLMCore::ToolResult r;
            r.content.append(LLMCore::ToolContent::makeText(
                QString("Image loaded: %1 (%2 bytes, %3)")
                    .arg(info.fileName())
                    .arg(bytes.size())
                    .arg(mimeName)));
            r.content.append(LLMCore::ToolContent::makeImage(bytes, mimeName));
            return r;
        });
    }
};

class SystemInfoTool : public LLMCore::BaseTool
{
    Q_OBJECT
public:
    explicit SystemInfoTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "system_info"; }
    QString displayName() const override { return "System Info"; }
    QString description() const override
    {
        return "Returns information about the current system (OS, hostname, CPU architecture). "
               "No parameters required.";
    }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{}},
        };
    }

    QFuture<LLMCore::ToolResult> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run([]() -> LLMCore::ToolResult {
            QJsonObject info;
            info["os"] = QSysInfo::prettyProductName();
            info["kernel"] = QSysInfo::kernelVersion();
            info["hostname"] = QSysInfo::machineHostName();
            info["cpu_arch"] = QSysInfo::currentCpuArchitecture();
            return LLMCore::ToolResult::text(QString::fromUtf8(
                QJsonDocument(info).toJson(QJsonDocument::Compact)));
        });
    }
};

} // namespace Example
