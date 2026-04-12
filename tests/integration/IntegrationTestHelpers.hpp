// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <LLMCore/BaseClient.hpp>
#include <LLMCore/BaseTool.hpp>
#include <LLMCore/ToolResult.hpp>
#include <LLMCore/ToolsManager.hpp>

namespace LLMCore::IntegrationTest {

// Timeout for network requests (30 seconds)
constexpr int kRequestTimeoutMs = 30000;
// Timeout for tool continuation (60 seconds, includes tool exec + second request)
constexpr int kToolContinuationTimeoutMs = 60000;

// --- Environment helpers ---

inline void skipIfNoEnv(const char *envVar)
{
    if (qgetenv(envVar).isEmpty()) {
        GTEST_SKIP() << envVar << " not set, skipping integration test";
    }
}

inline QString getEnvOrSkip(const char *envVar)
{
    skipIfNoEnv(envVar);
    return QString::fromUtf8(qgetenv(envVar));
}

inline QString getEnvOrDefault(const char *envVar, const QString &defaultValue)
{
    const QByteArray value = qgetenv(envVar);
    return value.isEmpty() ? defaultValue : QString::fromUtf8(value);
}

// --- A simple test tool that echoes its input ---

class EchoTool : public BaseTool
{
    Q_OBJECT
public:
    explicit EchoTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "echo"; }
    QString displayName() const override { return "Echo"; }
    QString description() const override
    {
        return "Echoes the input message back. Call this tool with a 'message' parameter "
               "containing any text, and it will return that exact text back to you.";
    }

    QJsonObject parametersSchema() const override
    {
        QJsonObject properties{
            {"message",
             QJsonObject{{"type", "string"}, {"description", "The message to echo back"}}}};

        return QJsonObject{
            {"type", "object"}, {"properties", properties}, {"required", QJsonArray{"message"}}};
    }

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> ToolResult {
            return ToolResult::text(input.value("message").toString("(no message)"));
        });
    }
};

// --- A tool that returns a tiny PNG as a rich tool-result content block ---

// Small, well-formed 10x10 PNG used as the canonical "image from a tool"
// payload in integration tests. Kept in sync with the existing
// ClaudeIntegrationTest.ImageMessage_Base64 asset so multi-provider tests
// agree on what the model should describe.
inline constexpr const char *kTinyPngBase64
    = "iVBORw0KGgoAAAANSUhEUgAAAAoAAAAKCAIAAAACUFjqAAAAEklEQVR4nGP4"
      "z8CAB+GTG8HSALfKY52fTcuYAAAAAElFTkSuQmCC";

class ImageReturningTool : public BaseTool
{
    Q_OBJECT
public:
    explicit ImageReturningTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "get_sample_image"; }
    QString displayName() const override { return "Get Sample Image"; }
    QString description() const override
    {
        return "Returns a small sample PNG image. Call this tool with no "
               "arguments to receive a bitmap that you can then describe.";
    }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"}, {"properties", QJsonObject{}}, {"required", QJsonArray{}}};
    }

    QFuture<ToolResult> executeAsync(const QJsonObject & /*input*/) override
    {
        return QtConcurrent::run([]() -> ToolResult {
            const QByteArray png = QByteArray::fromBase64(QByteArray(kTinyPngBase64));
            ToolResult r;
            r.content.append(ToolContent::makeText("Here is the sample image:"));
            r.content.append(ToolContent::makeImage(png, "image/png"));
            return r;
        });
    }
};

// --- A calculator tool for more complex tool use scenarios ---

class CalculatorTool : public BaseTool
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

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> ToolResult {
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
                    return ToolResult::error(QStringLiteral("division by zero"));
                result = a / b;
            } else {
                return ToolResult::error("unknown operation '" + op + "'");
            }

            return ToolResult::text(QString::number(result));
        });
    }
};

// --- Result collector for async tests with diagnostics ---

struct TestResult
{
    bool completed = false;
    bool failed = false;
    bool timedOut = false;
    QString fullText;
    QString errorMessage;
    QStringList chunks;
    QList<QPair<QString, QString>> thinkingBlocks; // {thinking, signature}
    QList<QPair<QString, QString>> toolCalls;      // {toolName, result}

    // Returns a diagnostic summary string for use in EXPECT/ASSERT messages
    std::string diagnostics() const
    {
        QString diag;
        diag += QString("  completed:  %1\n").arg(completed ? "true" : "false");
        diag += QString("  failed:     %1\n").arg(failed ? "true" : "false");
        diag += QString("  timedOut:   %1\n").arg(timedOut ? "true" : "false");
        diag += QString("  error:      '%1'\n").arg(errorMessage);
        diag += QString("  chunks:     %1\n").arg(chunks.size());
        diag += QString("  toolCalls:  %1\n").arg(toolCalls.size());
        diag += QString("  thinking:   %1\n").arg(thinkingBlocks.size());
        if (!fullText.isEmpty()) {
            QString preview = fullText.left(200);
            if (fullText.size() > 200)
                preview += "...";
            diag += QString("  fullText:   '%1'\n").arg(preview);
        } else {
            diag += QString("  fullText:   (empty)\n");
        }
        for (const auto &[name, res] : toolCalls) {
            diag += QString("  tool:       %1 -> %2\n").arg(name, res.left(100));
        }
        return diag.toStdString();
    }
};

// --- Helper to wire all callbacks to TestResult with logging ---

inline RequestCallbacks makeLoggingCallbacks(TestResult &result, QEventLoop &loop)
{
    RequestCallbacks callbacks;
    callbacks.onChunk = [&result](const RequestID &, const QString &chunk) {
        result.chunks.append(chunk);
    };
    callbacks.onAccumulated = [&result](const RequestID &, const QString &acc) {
        result.fullText = acc;
    };
    callbacks.onThinkingBlock =
        [&result](const RequestID &, const QString &thinking, const QString &signature) {
            result.thinkingBlocks.append({thinking, signature});
        };
    callbacks.onToolStarted =
        [&result](const RequestID &, const QString &toolId, const QString &name) {
            result.toolCalls.append({name, QString("started:%1").arg(toolId)});
        };
    callbacks.onToolResult =
        [&result](const RequestID &, const QString &toolId, const QString &name, const QString &res) {
            result.toolCalls.append({name + "_result", res});
            Q_UNUSED(toolId);
        };
    callbacks.onCompleted = [&result, &loop](const RequestID &, const QString &fullText) {
        result.completed = true;
        result.fullText = fullText;
        loop.quit();
    };
    callbacks.onFailed = [&result, &loop](const RequestID &, const QString &error) {
        result.failed = true;
        result.errorMessage = error;
        loop.quit();
    };
    return callbacks;
}

// --- Helper to run event loop with timeout and mark result ---

inline void waitWithTimeout(QEventLoop &loop, TestResult &result, int timeoutMs)
{
    QTimer::singleShot(timeoutMs, &loop, [&loop, &result]() {
        result.timedOut = true;
        loop.quit();
    });
    loop.exec();
}

// --- Test fixture base class ---

class ProviderTestBase : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "integration_tests";
            static char *argv[] = {arg0};
            m_app = new QCoreApplication(argc, argv);
        }
    }

    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }

    // Helper to wait for a signal with timeout
    static bool waitForSignal(QSignalSpy &spy, int timeoutMs = kRequestTimeoutMs)
    {
        if (spy.count() > 0)
            return true;
        return spy.wait(timeoutMs);
    }

    QCoreApplication *m_app = nullptr;
};

} // namespace LLMCore::IntegrationTest
