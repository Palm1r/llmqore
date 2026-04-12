// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtConcurrent/QtConcurrent>

#include "tools/ToolHandler.hpp"
#include <LLMCore/BaseTool.hpp>
#include <LLMCore/ToolExceptions.hpp>
#include <LLMCore/ToolResult.hpp>

using namespace LLMCore;

class SuccessTool : public BaseTool
{
    Q_OBJECT
public:
    explicit SuccessTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "success_tool"; }
    QString displayName() const override { return "Success Tool"; }
    QString description() const override { return "Always succeeds"; }
    QJsonObject parametersSchema() const override { return {}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> ToolResult {
            return ToolResult::text("result: " + input.value("key").toString("default"));
        });
    }
};

class FailingTool : public BaseTool
{
    Q_OBJECT
public:
    explicit FailingTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "failing_tool"; }
    QString displayName() const override { return "Failing Tool"; }
    QString description() const override { return "Always fails"; }
    QJsonObject parametersSchema() const override { return {}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run(
            []() -> ToolResult { throw ToolRuntimeError("Tool execution failed"); });
    }
};

class InvalidArgTool : public BaseTool
{
    Q_OBJECT
public:
    explicit InvalidArgTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "invalid_arg_tool"; }
    QString displayName() const override { return "Invalid Arg Tool"; }
    QString description() const override { return "Throws invalid argument"; }
    QJsonObject parametersSchema() const override { return {}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run(
            []() -> ToolResult { throw ToolInvalidArgument("Missing required field 'path'"); });
    }
};

class StdExceptionTool : public BaseTool
{
    Q_OBJECT
public:
    explicit StdExceptionTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "std_exception_tool"; }
    QString displayName() const override { return "StdException Tool"; }
    QString description() const override { return "Throws std::exception"; }
    QJsonObject parametersSchema() const override { return {}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run(
            []() -> ToolResult { throw std::runtime_error("std runtime error"); });
    }
};

class SlowTool : public BaseTool
{
    Q_OBJECT
public:
    explicit SlowTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}

    QString id() const override { return "slow_tool"; }
    QString displayName() const override { return "Slow Tool"; }
    QString description() const override { return "Takes time"; }
    QJsonObject parametersSchema() const override { return {}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        return QtConcurrent::run([]() -> ToolResult {
            QThread::msleep(500);
            return ToolResult::text("slow result");
        });
    }
};

class ToolHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_ToolHandler";
            static char *argv[] = {arg0};
            m_app = new QCoreApplication(argc, argv);
        }
    }

    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }

    QCoreApplication *m_app = nullptr;
};

TEST_F(ToolHandlerTest, ExecuteSuccess)
{
    ToolHandler handler;
    SuccessTool tool;
    QSignalSpy completedSpy(&handler, &ToolHandler::toolCompleted);
    QSignalSpy failedSpy(&handler, &ToolHandler::toolFailed);

    QJsonObject input{{"key", "hello"}};
    handler.executeToolAsync("req-1", "tool-1", &tool, input);

    EXPECT_TRUE(completedSpy.wait(3000));
    EXPECT_EQ(completedSpy.count(), 1);
    EXPECT_EQ(failedSpy.count(), 0);

    EXPECT_EQ(completedSpy[0][0].toString(), "req-1");
    EXPECT_EQ(completedSpy[0][1].toString(), "tool-1");
    const ToolResult result = completedSpy[0][2].value<ToolResult>();
    EXPECT_EQ(result.asText(), "result: hello");
}

TEST_F(ToolHandlerTest, ExecuteSuccess_DefaultInput)
{
    ToolHandler handler;
    SuccessTool tool;
    QSignalSpy completedSpy(&handler, &ToolHandler::toolCompleted);

    handler.executeToolAsync("req-1", "tool-1", &tool, {});

    EXPECT_TRUE(completedSpy.wait(3000));
    const ToolResult result = completedSpy[0][2].value<ToolResult>();
    EXPECT_EQ(result.asText(), "result: default");
}

TEST_F(ToolHandlerTest, ExecuteFailure_ToolRuntimeError)
{
    ToolHandler handler;
    FailingTool tool;
    QSignalSpy completedSpy(&handler, &ToolHandler::toolCompleted);
    QSignalSpy failedSpy(&handler, &ToolHandler::toolFailed);

    handler.executeToolAsync("req-1", "tool-1", &tool, {});

    EXPECT_TRUE(failedSpy.wait(3000));
    EXPECT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(completedSpy.count(), 0);

    EXPECT_EQ(failedSpy[0][0].toString(), "req-1");
    EXPECT_EQ(failedSpy[0][1].toString(), "tool-1");
    EXPECT_TRUE(failedSpy[0][2].toString().contains("Tool execution failed"));
}

TEST_F(ToolHandlerTest, ExecuteFailure_ToolInvalidArgument)
{
    ToolHandler handler;
    InvalidArgTool tool;
    QSignalSpy failedSpy(&handler, &ToolHandler::toolFailed);

    handler.executeToolAsync("req-1", "tool-1", &tool, {});

    EXPECT_TRUE(failedSpy.wait(3000));
    EXPECT_EQ(failedSpy.count(), 1);
    EXPECT_TRUE(failedSpy[0][2].toString().contains("Missing required field"));
}

TEST_F(ToolHandlerTest, ExecuteFailure_StdException)
{
    ToolHandler handler;
    StdExceptionTool tool;
    QSignalSpy failedSpy(&handler, &ToolHandler::toolFailed);

    handler.executeToolAsync("req-1", "tool-1", &tool, {});

    EXPECT_TRUE(failedSpy.wait(3000));
    EXPECT_EQ(failedSpy.count(), 1);
    // std::exception is caught; the exact message may be wrapped by QFuture
    EXPECT_FALSE(failedSpy[0][2].toString().isEmpty());
}

TEST_F(ToolHandlerTest, ExecuteNullTool)
{
    ToolHandler handler;
    QSignalSpy failedSpy(&handler, &ToolHandler::toolFailed);

    // Null tool should not crash and should emit toolFailed asynchronously.
    auto future = handler.executeToolAsync("req-1", "tool-1", nullptr, {});
    EXPECT_FALSE(future.isValid());

    EXPECT_TRUE(failedSpy.wait(3000));
    EXPECT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(failedSpy[0][0].toString(), "req-1");
    EXPECT_EQ(failedSpy[0][1].toString(), "tool-1");
    EXPECT_TRUE(failedSpy[0][2].toString().contains("null"));
}

TEST_F(ToolHandlerTest, MultipleToolsInParallel)
{
    ToolHandler handler;
    SuccessTool tool1, tool2;
    QSignalSpy completedSpy(&handler, &ToolHandler::toolCompleted);

    handler.executeToolAsync("req-1", "tool-1", &tool1, {{"key", "a"}});
    handler.executeToolAsync("req-1", "tool-2", &tool2, {{"key", "b"}});

    // Wait for both to complete
    if (completedSpy.count() < 2)
        EXPECT_TRUE(completedSpy.wait(3000));
    if (completedSpy.count() < 2)
        EXPECT_TRUE(completedSpy.wait(3000));
    EXPECT_EQ(completedSpy.count(), 2);

    QStringList results;
    results << completedSpy[0][2].value<ToolResult>().asText();
    results << completedSpy[1][2].value<ToolResult>().asText();
    EXPECT_TRUE(results.contains("result: a"));
    EXPECT_TRUE(results.contains("result: b"));
}

TEST_F(ToolHandlerTest, CleanupRequest)
{
    ToolHandler handler;
    SlowTool tool;

    handler.executeToolAsync("req-1", "tool-1", &tool, {});

    // Cleanup should not crash even while tool is running
    handler.cleanupRequest("req-1");

    // Cleanup of nonexistent request should not crash
    handler.cleanupRequest("req-nonexistent");
}

TEST_F(ToolHandlerTest, CleanupRequest_OnlyAffectsMatchingRequest)
{
    ToolHandler handler;
    SuccessTool tool1;
    SlowTool tool2;
    QSignalSpy completedSpy(&handler, &ToolHandler::toolCompleted);

    handler.executeToolAsync("req-1", "tool-1", &tool1, {{"key", "fast"}});
    handler.executeToolAsync("req-2", "tool-2", &tool2, {});

    // Wait for fast tool to complete
    EXPECT_TRUE(completedSpy.wait(3000));

    // Cleanup only req-2
    handler.cleanupRequest("req-2");

    // req-1 should have completed normally
    EXPECT_EQ(completedSpy[0][0].toString(), "req-1");
}

#include "tst_ToolHandler.moc"
