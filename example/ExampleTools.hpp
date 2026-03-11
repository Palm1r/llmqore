// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
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

    QFuture<QString> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run(
            []() -> QString { return QDateTime::currentDateTime().toString(Qt::ISODate); });
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

    QFuture<QString> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> QString {
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
                    return QString("Error: division by zero");
                result = a / b;
            } else {
                return QString("Error: unknown operation '%1'").arg(op);
            }

            return QString::number(result, 'g', 10);
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

    QFuture<QString> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run([]() -> QString {
            QJsonObject info;
            info["os"] = QSysInfo::prettyProductName();
            info["kernel"] = QSysInfo::kernelVersion();
            info["hostname"] = QSysInfo::machineHostName();
            info["cpu_arch"] = QSysInfo::currentCpuArchitecture();
            return QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact));
        });
    }
};

} // namespace Example
