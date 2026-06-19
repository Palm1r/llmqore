// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QTimer>

#include <LLMQore/CallbackPermissionProvider.hpp>
#include <LLMQore/DefaultFileSystemProvider.hpp>
#include <LLMQore/RpcExceptions.hpp>
#include <LLMQore/TerminalManager.hpp>

using namespace LLMQore;
using namespace LLMQore::Acp;

namespace {

template<typename T>
T waitForFuture(const QFuture<T> &future, int timeoutMs = 5000)
{
    if (future.isFinished())
        return future.result();
    QEventLoop loop;
    QFutureWatcher<T> watcher;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return future.result();
}

void waitForVoidFuture(const QFuture<void> &future, int timeoutMs = 5000)
{
    if (future.isFinished())
        return;
    QEventLoop loop;
    QFutureWatcher<void> watcher;
    QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
}

class AcpProvidersTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_AcpProviders";
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

} // namespace

TEST_F(AcpProvidersTest, DefaultFileSystemWriteThenRead)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("note.txt");

    DefaultFileSystemProvider fs;
    waitForVoidFuture(fs.writeTextFile("s", path, "alpha\nbeta\n"));

    const QString content = waitForFuture(fs.readTextFile("s", path, std::nullopt, std::nullopt));
    EXPECT_EQ(content, "alpha\nbeta\n");
}

TEST_F(AcpProvidersTest, DefaultFileSystemReadSlice)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("lines.txt");

    DefaultFileSystemProvider fs;
    waitForVoidFuture(fs.writeTextFile("s", path, "l1\nl2\nl3\nl4"));

    // 1-based line 2, limit 2 -> l2, l3
    const QString slice = waitForFuture(fs.readTextFile("s", path, 2, 2));
    EXPECT_EQ(slice, "l2\nl3");
}

TEST_F(AcpProvidersTest, DefaultFileSystemReadMissingRejects)
{
    DefaultFileSystemProvider fs;
    QFuture<QString> f = fs.readTextFile("s", "/no/such/file-xyz.txt", std::nullopt, std::nullopt);
    bool threw = false;
    try {
        waitForFuture(f);
        (void) f.result();
    } catch (const Rpc::JsonRpcException &) {
        threw = true;
    } catch (...) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST_F(AcpProvidersTest, DefaultFileSystemWriteDisabled)
{
    DefaultFileSystemProvider fs;
    fs.setWritable(false);
    EXPECT_FALSE(fs.supportsWrite());

    QFuture<void> f = fs.writeTextFile("s", "/tmp/should-not-write.txt", "x");
    bool threw = false;
    try {
        waitForVoidFuture(f);
        f.waitForFinished();
    } catch (const Rpc::JsonRpcException &) {
        threw = true;
    } catch (...) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST_F(AcpProvidersTest, CallbackPermissionReturnsConfiguredOutcome)
{
    CallbackPermissionProvider provider(
        [](const QString &, const ToolCall &, const QList<PermissionOption> &opts) {
            return RequestPermissionResult::selected(opts.first().optionId);
        });

    ToolCall tc;
    tc.toolCallId = "c1";
    QList<PermissionOption> opts{
        PermissionOption{"yes", "Allow", PermissionOptionKind::AllowOnce}};

    const RequestPermissionResult r
        = waitForFuture(provider.requestPermission("s", tc, opts));
    EXPECT_EQ(r.outcome, "selected");
    EXPECT_EQ(r.optionId, "yes");
}

TEST_F(AcpProvidersTest, TerminalRunsCommandAndReportsExit)
{
    TerminalManager tm;

    CreateTerminalParams p;
    p.sessionId = "s";
#ifdef Q_OS_WIN
    p.command = "cmd";
    p.args = QStringList{"/c", "echo hello"};
#else
    p.command = "/bin/echo";
    p.args = QStringList{"hello"};
#endif

    const CreateTerminalResult created = waitForFuture(tm.createTerminal(p));
    ASSERT_FALSE(created.terminalId.isEmpty());

    const WaitForTerminalExitResult exit
        = waitForFuture(tm.waitForExit("s", created.terminalId));
    ASSERT_TRUE(exit.exitCode.has_value());
    EXPECT_EQ(*exit.exitCode, 0);

    const TerminalOutputResult out
        = waitForFuture(tm.terminalOutput("s", created.terminalId));
    EXPECT_TRUE(out.output.contains("hello"));
    ASSERT_TRUE(out.exitStatus.has_value());
    ASSERT_TRUE(out.exitStatus->exitCode.has_value());
    EXPECT_EQ(*out.exitStatus->exitCode, 0);

    waitForVoidFuture(tm.releaseTerminal("s", created.terminalId));
}

TEST_F(AcpProvidersTest, TerminalUnknownIdRejects)
{
    TerminalManager tm;
    QFuture<TerminalOutputResult> f = tm.terminalOutput("s", "nope");
    bool threw = false;
    try {
        waitForFuture(f);
        (void) f.result();
    } catch (...) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}
