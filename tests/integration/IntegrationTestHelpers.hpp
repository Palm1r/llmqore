// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStringList>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/BaseTool.hpp>
#include <LLMQore/Conversation.hpp>
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/ToolResult.hpp>
#include <LLMQore/ToolsManager.hpp>

namespace LLMQore::IntegrationTest {

constexpr int kRequestTimeoutMs = 30000;
constexpr int kToolContinuationTimeoutMs = 60000;

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

inline QStringList childSearchPath()
{
    QStringList entries = QString::fromUtf8(qgetenv("LLMQORE_ENV_PATH"))
                              .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    const QStringList inherited
        = QString::fromUtf8(qgetenv("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &entry : inherited) {
        if (!entries.contains(entry))
            entries.append(entry);
    }
    return entries;
}

inline QProcessEnvironment childEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), childSearchPath().join(QLatin1Char(':')));
    return environment;
}

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
            r.content.append(TextContent{"Here is the sample image:"});
            r.content.append(ImageContent::fromBytes(png, "image/png"));
            return r;
        });
    }
};

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

struct TestResult
{
    bool completed = false;
    bool failed = false;
    bool timedOut = false;
    QString fullText;
    QString errorMessage;
    QStringList chunks;
    QList<QPair<QString, QString>> thinkingBlocks;
    QList<QPair<QString, QString>> toolCalls;
    Conversation conversation;

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

inline void wireLoggingSignals(
    BaseClient *client, TestResult &result, QEventLoop &loop, QObject *scope = nullptr)
{
    if (!scope)
        scope = &loop;

    QObject::connect(client, &BaseClient::requestFinalized, scope,
                     [&result](const RequestID &, const CompletionInfo &info) {
        result.conversation = info.conversation;
    });

    QObject::connect(client, &BaseClient::chunkReceived, scope,
                     [&result](const RequestID &, const QString &chunk) {
                         result.chunks.append(chunk);
                     });
    QObject::connect(client, &BaseClient::accumulatedReceived, scope,
                     [&result](const RequestID &, const QString &acc) {
                         result.fullText = acc;
                     });
    QObject::connect(
        client, &BaseClient::thinkingBlockReceived, scope,
        [&result](const RequestID &, const QString &thinking, const QString &signature) {
            result.thinkingBlocks.append({thinking, signature});
        });
    QObject::connect(client, &BaseClient::toolStarted, scope,
                     [&result](const RequestID &, const QString &toolId, const QString &name) {
                         result.toolCalls.append({name, QString("started:%1").arg(toolId)});
                     });
    QObject::connect(
        client, &BaseClient::toolResultReady, scope,
        [&result](const RequestID &, const QString &toolId, const QString &name, const QString &res) {
            result.toolCalls.append({name + "_result", res});
            Q_UNUSED(toolId);
        });
    QObject::connect(client, &BaseClient::requestCompleted, scope,
                     [&result, &loop](const RequestID &, const QString &fullText) {
                         result.completed = true;
                         result.fullText = fullText;
                         loop.quit();
                     });
    QObject::connect(client, &BaseClient::requestFailed, scope,
                     [&result, &loop](const RequestID &, const QString &error) {
                         result.failed = true;
                         result.errorMessage = error;
                         loop.quit();
                     });
}

inline void waitWithTimeout(
    QEventLoop &loop, TestResult &result, int timeoutMs, QObject *scope = nullptr)
{
    QTimer::singleShot(timeoutMs, scope ? scope : static_cast<QObject *>(&loop),
                       [&loop, &result]() {
                           result.timedOut = true;
                           loop.quit();
                       });
    loop.exec();
}

[[nodiscard]] inline bool isRateLimitError(const QString &error)
{
    return error.startsWith(QLatin1String("HTTP 429"))
           || error.contains(QLatin1String("(code: 429)"))
           || error.contains(QLatin1String("rate_limit"), Qt::CaseInsensitive);
}

[[nodiscard]] inline bool isRateLimited(const TestResult &result)
{
    return result.failed && isRateLimitError(result.errorMessage);
}

#define LLMQORE_SKIP_IF_RATE_LIMITED(result)                                                   \
    if (::LLMQore::IntegrationTest::isRateLimited(result))                                      \
    GTEST_SKIP() << "provider rate limit, not a code failure: "                                 \
                 << (result).errorMessage.toStdString()

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

    static bool waitForSignal(QSignalSpy &spy, int timeoutMs = kRequestTimeoutMs)
    {
        if (spy.count() > 0)
            return true;
        return spy.wait(timeoutMs);
    }

    QCoreApplication *m_app = nullptr;
};

inline TestResult runConversation(
    BaseClient *client, const Conversation &conversation, QEventLoop &loop)
{
    TestResult result;
    QObject scope;

    wireLoggingSignals(client, result, loop, &scope);
    client->ask(conversation);
    waitWithTimeout(loop, result, kRequestTimeoutMs, &scope);

    return result;
}

inline void expectMultiTurnAccepted(BaseClient *client)
{
    Conversation conversation;
    conversation.setSystem("Answer with a single word.");
    conversation.addUser("What is the capital of France?");

    QEventLoop firstLoop;
    TestResult first = runConversation(client, conversation, firstLoop);
    LLMQORE_SKIP_IF_RATE_LIMITED(first);

    ASSERT_FALSE(first.timedOut) << "First turn timed out\n" << first.diagnostics();
    ASSERT_TRUE(first.completed) << first.diagnostics();
    ASSERT_FALSE(first.failed) << first.diagnostics();
    ASSERT_FALSE(first.fullText.isEmpty()) << first.diagnostics();

    Conversation carried = first.conversation;
    ASSERT_EQ(carried.turns().size(), 2)
        << "assistant turn missing from CompletionInfo::conversation";
    EXPECT_EQ(carried.turns()[1].role, TurnRole::Assistant);

    carried.addUser("And of Italy?");

    QEventLoop secondLoop;
    TestResult second = runConversation(client, carried, secondLoop);
    LLMQORE_SKIP_IF_RATE_LIMITED(second);

    ASSERT_FALSE(second.timedOut) << "Second turn timed out\n" << second.diagnostics();
    ASSERT_TRUE(second.completed) << "Provider rejected the replayed history\n"
                                  << second.diagnostics();
    ASSERT_FALSE(second.failed) << second.diagnostics();
    EXPECT_FALSE(second.fullText.isEmpty()) << second.diagnostics();
}

inline void expectAskOnceResolves(BaseClient *client)
{
    QEventLoop loop;
    QObject scope;
    bool settled = false;
    CompletionInfo received;
    QString error;

    LLMQore::compat(client->askOnce("Reply with exactly: ok"))
        .then(&scope, [&](const CompletionInfo &info) -> int {
            received = info;
            settled = true;
            loop.quit();
            return 0;
        })
        .onFailed(&scope, [&](const std::exception &e) -> int {
            error = QString::fromUtf8(e.what());
            settled = true;
            loop.quit();
            return 0;
        });

    QTimer::singleShot(kRequestTimeoutMs, &scope, [&loop]() { loop.quit(); });
    loop.exec();

    ASSERT_TRUE(settled) << "askOnce future never settled";
    if (isRateLimitError(error))
        GTEST_SKIP() << "provider rate limit, not a code failure: " << error.toStdString();
    ASSERT_TRUE(error.isEmpty()) << "askOnce rejected: " << error.toStdString();
    EXPECT_FALSE(received.fullText.isEmpty());
}

} // namespace LLMQore::IntegrationTest
