// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QPromise>
#include <QSet>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/BaseElicitationProvider.hpp>
#include <LLMQore/BasePromptProvider.hpp>
#include <LLMQore/BaseResourceProvider.hpp>
#include <LLMQore/BaseRootsProvider.hpp>
#include <LLMQore/BaseTool.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/RpcExceptions.hpp>
#include <LLMQore/RpcPipeTransport.hpp>
#include <LLMQore/McpServer.hpp>
#include <LLMQore/JsonRpcSession.hpp>
#include "clients/claude/ClaudeMessage.hpp"
#include "clients/openai/OpenAIMessage.hpp"
#include "LoopbackHarness.hpp"
#include "TestHelpers.hpp"
#include <LLMQore/ToolRegistry.hpp>
#include <LLMQore/ToolsManager.hpp>

using namespace LLMQore;
using namespace LLMQore::Mcp;

namespace {

class EchoTool : public BaseTool
{
    Q_OBJECT
public:
    explicit EchoTool(QObject *parent = nullptr)
        : BaseTool(parent)
    {}
    QString id() const override { return "echo"; }
    QString displayName() const override { return "Echo"; }
    QString description() const override { return "Echoes its input text"; }
    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{{"text", QJsonObject{{"type", "string"}}}}},
            {"required", QJsonArray{"text"}},
        };
    }
    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        const QString text = input.value("text").toString();
        return QtConcurrent::run([text]() -> LLMQore::ToolResult {
            return LLMQore::ToolResult::text(QString("echo: %1").arg(text));
        });
    }
};

class MemoryResourceProvider : public BaseResourceProvider
{
    Q_OBJECT
public:
    explicit MemoryResourceProvider(QObject *parent = nullptr)
        : BaseResourceProvider(parent)
    {}

    void addResource(const QString &uri, const QString &text)
    {
        m_resources.insert(uri, text);
    }

    void addTemplate(const QString &uriTemplate, const QString &name)
    {
        ResourceTemplate t;
        t.uriTemplate = uriTemplate;
        t.name = name;
        t.mimeType = "text/plain";
        m_templates.append(t);
    }

    QFuture<QList<ResourceInfo>> listResources() override
    {
        QList<ResourceInfo> list;
        for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
            ResourceInfo info;
            info.uri = it.key();
            info.name = it.key();
            info.mimeType = "text/plain";
            list.append(info);
        }
        auto promise = std::make_shared<QPromise<QList<ResourceInfo>>>();
        promise->start();
        promise->addResult(list);
        promise->finish();
        return promise->future();
    }

    QFuture<QList<ResourceTemplate>> listResourceTemplates() override
    {
        auto promise = std::make_shared<QPromise<QList<ResourceTemplate>>>();
        promise->start();
        promise->addResult(m_templates);
        promise->finish();
        return promise->future();
    }

    QFuture<ResourceContents> readResource(const QString &uri) override
    {
        auto promise = std::make_shared<QPromise<ResourceContents>>();
        promise->start();
        if (m_resources.contains(uri)) {
            ResourceContents c;
            c.uri = uri;
            c.mimeType = "text/plain";
            c.text = m_resources.value(uri);
            promise->addResult(c);
        } else {
            promise->addResult(ResourceContents{});
        }
        promise->finish();
        return promise->future();
    }

    void addCompletion(
        const QString &templateUri,
        const QString &placeholderName,
        const QStringList &suggestions)
    {
        m_completions.insert(QString("%1|%2").arg(templateUri, placeholderName), suggestions);
    }

    QFuture<CompletionResult> completeArgument(
        const QString &templateUri,
        const QString &placeholderName,
        const QString &partialValue,
        const QJsonObject & /*contextArguments*/) override
    {
        auto promise = std::make_shared<QPromise<CompletionResult>>();
        promise->start();
        CompletionResult r;
        const QString key = QString("%1|%2").arg(templateUri, placeholderName);
        for (const QString &s : m_completions.value(key)) {
            if (s.startsWith(partialValue))
                r.values.append(s);
        }
        promise->addResult(r);
        promise->finish();
        return promise->future();
    }

    bool supportsSubscription() const override { return m_supportsSubscription; }
    void subscribe(const QString &uri) override { m_subscribed.insert(uri); }
    void unsubscribe(const QString &uri) override { m_subscribed.remove(uri); }

    void setSupportsSubscription(bool on) { m_supportsSubscription = on; }
    bool isSubscribed(const QString &uri) const { return m_subscribed.contains(uri); }

    void touch(const QString &uri, const QString &text)
    {
        m_resources.insert(uri, text);
        if (m_subscribed.contains(uri))
            emit resourceUpdated(uri);
    }

private:
    QHash<QString, QString> m_resources;
    QHash<QString, QStringList> m_completions;
    QList<ResourceTemplate> m_templates;
    QSet<QString> m_subscribed;
    bool m_supportsSubscription = false;
};

class MemoryPromptProvider : public BasePromptProvider
{
    Q_OBJECT
public:
    explicit MemoryPromptProvider(QObject *parent = nullptr)
        : BasePromptProvider(parent)
    {}

    QFuture<QList<PromptInfo>> listPrompts() override
    {
        PromptInfo info;
        info.name = "greet";
        info.description = "Generates a greeting";
        PromptArgument arg;
        arg.name = "name";
        arg.description = "Who to greet";
        arg.required = true;
        info.arguments = {arg};

        auto promise = std::make_shared<QPromise<QList<PromptInfo>>>();
        promise->start();
        promise->addResult({info});
        promise->finish();
        return promise->future();
    }

    QFuture<PromptGetResult> getPrompt(
        const QString &name, const QJsonObject &arguments) override
    {
        auto promise = std::make_shared<QPromise<PromptGetResult>>();
        promise->start();
        if (name != "greet") {
            promise->setException(std::make_exception_ptr(Rpc::RemoteError(
                Rpc::ErrorCode::InvalidParams, QString("Unknown prompt: %1").arg(name))));
            promise->finish();
            return promise->future();
        }
        const QString who = arguments.value("name").toString("world");

        PromptGetResult result;
        result.description = "Greeting";
        PromptMessage m;
        m.role = "user";
        m.content = QJsonObject{{"type", "text"}, {"text", QString("Hello, %1!").arg(who)}};
        result.messages.append(m);
        promise->addResult(result);
        promise->finish();
        return promise->future();
    }

    QFuture<CompletionResult> completeArgument(
        const QString &promptName,
        const QString &argumentName,
        const QString &partialValue,
        const QJsonObject & /*contextArguments*/) override
    {
        auto promise = std::make_shared<QPromise<CompletionResult>>();
        promise->start();
        CompletionResult r;
        if (promptName == "greet" && argumentName == "name") {
            const QStringList candidates
                = {"alice", "bob", "charlie", "dave", "erin"};
            for (const QString &c : candidates) {
                if (c.startsWith(partialValue))
                    r.values.append(c);
            }
            r.total = r.values.size();
        }
        promise->addResult(r);
        promise->finish();
        return promise->future();
    }
};

class PlainPromptProvider : public BasePromptProvider
{
    Q_OBJECT
public:
    using BasePromptProvider::BasePromptProvider;
    QFuture<QList<PromptInfo>> listPrompts() override
    {
        auto p = std::make_shared<QPromise<QList<PromptInfo>>>();
        p->start();
        p->addResult({});
        p->finish();
        return p->future();
    }
    QFuture<PromptGetResult> getPrompt(
        const QString &, const QJsonObject &) override
    {
        auto p = std::make_shared<QPromise<PromptGetResult>>();
        p->start();
        p->addResult({});
        p->finish();
        return p->future();
    }
};

class FakeSamplingClient : public BaseClient
{
    Q_OBJECT
public:
    explicit FakeSamplingClient(QObject *parent = nullptr)
        : BaseClient({}, {}, QStringLiteral("fake-model-1.0"), parent)
    {}

    QString cannedText = QStringLiteral("pong");
    QString cannedStopReason = QStringLiteral("end_turn");

    QString cannedError;

    QJsonObject lastPayload;

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString & /*endpoint*/ = {},
        RequestMode /*mode*/ = RequestMode::Streaming) override
    {
        lastPayload = payload;
        const RequestID id = QStringLiteral("fake-req-1");

        const QString err = cannedError;
        const QString text = cannedText;
        const QString stop = cannedStopReason;
        const QString modelName = model();

        QTimer::singleShot(0, this, [this, id, err, text, stop, modelName]() {
            if (!err.isEmpty()) {
                emit requestFailed(id, err);
                return;
            }
            CompletionInfo info;
            info.fullText = text;
            info.model = modelName;
            info.stopReason = stop;
            emit requestFinalized(id, info);
            emit requestCompleted(id, text);
        });

        return id;
    }

    RequestID ask(
        const QString &prompt,
        RequestMode mode = RequestMode::Streaming) override
    {
        return sendMessage(QJsonObject{{"prompt", prompt}}, {}, mode);
    }

    QJsonObject buildConversationPayload(const Conversation &conversation) const override
    {
        return conversation.toJson();
    }

    QFuture<QList<ModelInfo>> listModels(const QString & = {}) override
    {
        QPromise<QList<ModelInfo>> p;
        p.start();
        p.addResult(QList<ModelInfo>{ModelInfo{QStringLiteral("fake-model-1.0")}});
        p.finish();
        return p.future();
    }

protected:
    const ToolDialect &toolDialect() const override { return OpenAIMessage::toolDialect(); }
    const UsageSchema &usageSchema() const override { return kNoUsageSchema; }
    void processData(const RequestID &, const QByteArray &) override {}
    void processBufferedBody(const RequestID &, const QJsonObject &) override {}
    QJsonObject buildContinuationPayload(
        const QJsonObject &,
        BaseMessage *,
        const QHash<QString, ToolResult> &) override
    {
        return {};
    }
};

class CannedElicitationProvider : public BaseElicitationProvider
{
    Q_OBJECT
public:
    explicit CannedElicitationProvider(QObject *parent = nullptr)
        : BaseElicitationProvider(parent)
    {}

    QFuture<ElicitResult> elicit(const ElicitRequestParams &params) override
    {
        lastParams = params;
        auto promise = std::make_shared<QPromise<ElicitResult>>();
        promise->start();
        ElicitResult r;
        r.action = ElicitAction::Accept;
        r.content = cannedContent;
        promise->addResult(r);
        promise->finish();
        return promise->future();
    }

    ElicitRequestParams lastParams;
    QJsonObject cannedContent = QJsonObject{{"username", "octocat"}};
};

class RefusingElicitationProvider : public BaseElicitationProvider
{
    Q_OBJECT
public:
    using BaseElicitationProvider::BaseElicitationProvider;

    QFuture<ElicitResult> elicit(const ElicitRequestParams &) override
    {
        auto promise = std::make_shared<QPromise<ElicitResult>>();
        promise->start();
        promise->setException(std::make_exception_ptr(
            Rpc::RemoteError(Rpc::ErrorCode::InvalidParams, QStringLiteral("Denied by user"))));
        promise->finish();
        return promise->future();
    }
};

class StaticRootsProvider : public BaseRootsProvider
{
    Q_OBJECT
public:
    explicit StaticRootsProvider(QObject *parent = nullptr)
        : BaseRootsProvider(parent)
    {}

    QFuture<QList<Root>> listRoots() override
    {
        QList<Root> roots;
        Root r;
        r.uri = "file:///workspace";
        r.name = "workspace";
        roots.append(r);
        auto promise = std::make_shared<QPromise<QList<Root>>>();
        promise->start();
        promise->addResult(roots);
        promise->finish();
        return promise->future();
    }
};

class ProgressiveTool : public BaseTool
{
    Q_OBJECT
public:
    ProgressiveTool(Mcp::McpServer *server, QObject *parent = nullptr)
        : BaseTool(parent)
        , m_server(server)
    {}
    QString id() const override { return "count"; }
    QString displayName() const override { return "Count"; }
    QString description() const override { return "Counts up, publishing progress"; }
    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{{"steps", QJsonObject{{"type", "integer"}}}}},
        };
    }
    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        const int steps = input.value("steps").toInt(3);
        const QString token = m_server->session()->currentProgressToken();
        Rpc::JsonRpcSession *session = m_server->session();
        return QtConcurrent::run([steps, token, session]() -> LLMQore::ToolResult {
            for (int i = 1; i <= steps; ++i) {
                QThread::msleep(5);
                if (!token.isEmpty()) {
                    QMetaObject::invokeMethod(
                        session,
                        [session, token, i, steps]() {
                            session->sendProgress(
                                token,
                                static_cast<double>(i),
                                static_cast<double>(steps),
                                QString("step %1").arg(i));
                        },
                        Qt::QueuedConnection);
                }
            }
            return LLMQore::ToolResult::text(QString("counted %1").arg(steps));
        });
    }

private:
    Mcp::McpServer *m_server;
};

class McpLoopbackTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_McpLoopback";
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

TEST_F(McpLoopbackTest, FullHandshakeListAndCallTool)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServerConfig cfg;
    cfg.serverInfo = {"loopback-server", "0.0.1"};
    McpServer server(serverTransport, cfg);
    server.addTool(new EchoTool(&server));

    McpClient client(clientTransport, Implementation{"loopback-client", "0.0.1"});

    server.start();

    QFuture<InitializeResult> initFuture = client.connectAndInitialize(std::chrono::seconds(5));
    const InitializeResult result = waitForFuture(initFuture);
    EXPECT_EQ(result.serverInfo.name, "loopback-server");
    EXPECT_TRUE(client.isInitialized());

    QFuture<QList<ToolInfo>> listFuture = client.listTools();
    const QList<ToolInfo> tools = waitForFuture(listFuture);
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools.first().name, "echo");

    QFuture<LLMQore::ToolResult> callFuture
        = client.callTool("echo", QJsonObject{{"text", "ping"}});
    const LLMQore::ToolResult callResult = waitForFuture(callFuture);
    EXPECT_FALSE(callResult.isError);
    EXPECT_EQ(callResult.asText(), "echo: ping");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, AddMcpClientRegistersToolsInToolsManager)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});
    server.addTool(new EchoTool(&server));

    McpClient client(clientTransport);
    ToolsManager manager(ClaudeMessage::toolDialect());

    server.start();
    waitForFuture(client.connectAndInitialize());

    manager.addMcpClient(&client);

    // Allow the async listTools to complete.
    QEventLoop loop;
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(manager.registeredTools().size(), 1);
    BaseTool *tool = manager.tool("echo");
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->id(), "echo");

    QFuture<LLMQore::ToolResult> exec
        = tool->executeAsync(QJsonObject{{"text", "via-manager"}});
    const LLMQore::ToolResult result = waitForFuture(exec);
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.asText(), "echo: via-manager");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, ToolsChangedNotificationRefreshesTools)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});
    server.addTool(new EchoTool(&server));

    McpClient client(clientTransport);
    ToolsManager manager(ClaudeMessage::toolDialect());

    server.start();
    waitForFuture(client.connectAndInitialize());

    manager.addMcpClient(&client);

    {
        QEventLoop loop;
        QTimer::singleShot(500, &loop, &QEventLoop::quit);
        loop.exec();
    }

    EXPECT_EQ(manager.registeredTools().size(), 1);

    QSignalSpy changedSpy(&client, &McpClient::toolsChanged);

    class UppercaseTool : public BaseTool
    {
    public:
        using BaseTool::BaseTool;
        QString id() const override { return "upper"; }
        QString displayName() const override { return "Upper"; }
        QString description() const override { return "Uppercases input"; }
        QJsonObject parametersSchema() const override
        {
            return QJsonObject{{"type", "object"}};
        }
        QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &) override
        {
            return QtConcurrent::run(
                []() -> LLMQore::ToolResult { return LLMQore::ToolResult::text("upper!"); });
        }
    };
    server.addTool(new UppercaseTool(&server));

    QEventLoop loop;
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    QObject::connect(&client, &McpClient::toolsChanged, &loop, &QEventLoop::quit);
    loop.exec();

    QTimer::singleShot(300, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_GE(changedSpy.size(), 1);
    EXPECT_EQ(manager.registeredTools().size(), 2);

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, RegistryToolAdditionSendsListChangedNotification)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});
    ToolRegistry registry;
    server.setToolRegistry(&registry);

    McpClient client(clientTransport);
    server.start();
    waitForFuture(client.connectAndInitialize());

    QSignalSpy changedSpy(&client, &McpClient::toolsChanged);

    registry.addTool(new EchoTool);

    QEventLoop loop;
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    QObject::connect(&client, &McpClient::toolsChanged, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_GE(changedSpy.size(), 1);

    const QList<ToolInfo> tools = waitForFuture(client.listTools());
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools.first().name, "echo");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, PingRoundTrips)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    McpClient client(clientTransport);
    server.start();
    waitForFuture(client.connectAndInitialize());

    waitForVoidFuture(client.ping(std::chrono::seconds(2)));

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, SetLogLevelIsRecordedOnServer)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    McpClient client(clientTransport);
    server.start();
    waitForFuture(client.connectAndInitialize());

    EXPECT_EQ(server.currentLogLevel(), "info");
    waitForVoidFuture(client.setLogLevel("debug"));
    EXPECT_EQ(server.currentLogLevel(), "debug");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, LogMessageNotificationFromServerToClient)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    McpClient client(clientTransport);

    QSignalSpy spy(&client, &McpClient::logMessage);

    server.start();
    waitForFuture(client.connectAndInitialize());

    server.sendLogMessage(
        "info", "test", QJsonValue(QJsonObject{{"k", "v"}}), "Hello from server");

    QEventLoop loop;
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    QObject::connect(&client, &McpClient::logMessage, &loop, &QEventLoop::quit);
    loop.exec();

    ASSERT_GE(spy.size(), 1);
    const auto args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toString(), "info");
    EXPECT_EQ(args.at(1).toString(), "test");
    EXPECT_EQ(args.at(3).toString(), "Hello from server");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, ResourceTemplatesListRoundTrips)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    auto *provider = new MemoryResourceProvider(&server);
    provider->addTemplate("mem://log/{date}", "logs");
    server.addResourceProvider(provider);

    McpClient client(clientTransport);
    server.start();
    waitForFuture(client.connectAndInitialize());

    const auto templates = waitForFuture(client.listResourceTemplates());
    ASSERT_EQ(templates.size(), 1);
    EXPECT_EQ(templates.first().uriTemplate, "mem://log/{date}");
    EXPECT_EQ(templates.first().name, "logs");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, PromptsListAndGet)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    server.addPromptProvider(new MemoryPromptProvider(&server));

    McpClient client(clientTransport);
    server.start();
    const auto init = waitForFuture(client.connectAndInitialize());
    ASSERT_TRUE(init.capabilities.prompts.has_value());

    const auto prompts = waitForFuture(client.listPrompts());
    ASSERT_EQ(prompts.size(), 1);
    EXPECT_EQ(prompts.first().name, "greet");
    ASSERT_EQ(prompts.first().arguments.size(), 1);
    EXPECT_EQ(prompts.first().arguments.first().name, "name");
    EXPECT_TRUE(prompts.first().arguments.first().required);

    const auto result
        = waitForFuture(client.getPrompt("greet", QJsonObject{{"name", "alice"}}));
    ASSERT_EQ(result.messages.size(), 1);
    EXPECT_EQ(result.messages.first().role, "user");
    EXPECT_EQ(result.messages.first().content.value("text").toString(), "Hello, alice!");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, CompletionCapabilityAdvertisedWithProviders)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    server.addPromptProvider(new MemoryPromptProvider(&server));

    McpClient client(clientTransport);
    server.start();
    const auto init = waitForFuture(client.connectAndInitialize());

    // With at least one provider, the server should advertise `completions`.
    EXPECT_TRUE(init.capabilities.completions.has_value());

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, CompletionForPromptArgumentReturnsFilteredSuggestions)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    server.addPromptProvider(new MemoryPromptProvider(&server));

    McpClient client(clientTransport);
    server.start();
    waitForFuture(client.connectAndInitialize());

    CompletionReference ref;
    ref.type = "ref/prompt";
    ref.name = "greet";

    CompletionResult r1 = waitForFuture(
        client.complete(ref, "name", "a"));
    EXPECT_EQ(r1.values.size(), 1);
    EXPECT_EQ(r1.values.first(), "alice");

    CompletionResult r2 = waitForFuture(client.complete(ref, "name", ""));
    EXPECT_EQ(r2.values.size(), 5);

    CompletionResult r3 = waitForFuture(
        client.complete(ref, "nonexistent", ""));
    EXPECT_TRUE(r3.values.isEmpty());

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, CompletionForResourceTemplatePlaceholder)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    auto *provider = new MemoryResourceProvider(&server);
    provider->addTemplate("file:///logs/{date}.log", "daily_logs");
    provider->addCompletion(
        "file:///logs/{date}.log",
        "date",
        {"2026-04-09", "2026-04-10", "2026-04-11"});
    server.addResourceProvider(provider);

    McpClient client(clientTransport);
    server.start();
    const auto init = waitForFuture(client.connectAndInitialize());
    EXPECT_TRUE(init.capabilities.completions.has_value());

    CompletionReference ref;
    ref.type = "ref/resource";
    ref.uri = "file:///logs/{date}.log";

    CompletionResult all = waitForFuture(client.complete(ref, "date", "2026"));
    EXPECT_EQ(all.values.size(), 3);

    CompletionResult narrow
        = waitForFuture(client.complete(ref, "date", "2026-04-11"));
    ASSERT_EQ(narrow.values.size(), 1);
    EXPECT_EQ(narrow.values.first(), "2026-04-11");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, CompletionDefaultProviderReturnsEmptyList)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    server.addPromptProvider(new PlainPromptProvider(&server));

    McpClient client(clientTransport);
    server.start();
    waitForFuture(client.connectAndInitialize());

    CompletionReference ref;
    ref.type = "ref/prompt";
    ref.name = "anything";
    CompletionResult r = waitForFuture(client.complete(ref, "arg", "val"));
    EXPECT_TRUE(r.values.isEmpty());

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, RootsListRoundTripsFromServerToClient)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    McpClient client(clientTransport);

    auto *roots = new StaticRootsProvider(&client);
    client.setRootsProvider(roots);

    server.start();
    waitForFuture(client.connectAndInitialize());

    QFuture<QJsonValue> raw
        = server.session()->sendRequest(QLatin1String(Mcp::Method::RootsList), QJsonObject{});
    const QJsonValue value = waitForFuture(raw);
    const QJsonArray arr = value.toObject().value("roots").toArray();
    ASSERT_EQ(arr.size(), 1);
    EXPECT_EQ(arr.first().toObject().value("uri").toString(), "file:///workspace");
    EXPECT_EQ(arr.first().toObject().value("name").toString(), "workspace");

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, ProgressNotificationsDeliveredToCallback)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});
    auto *tool = new ProgressiveTool(&server, &server);
    server.addTool(tool);

    McpClient client(clientTransport);
    server.start();
    waitForFuture(client.connectAndInitialize());

    struct Step
    {
        double progress;
        double total;
        QString message;
    };
    auto steps = std::make_shared<QList<Step>>();

    auto call = client.callToolWithProgress(
        "count",
        QJsonObject{{"steps", 3}},
        [steps](double p, double t, const QString &m) {
            steps->append({p, t, m});
        });

    const LLMQore::ToolResult result = waitForFuture(call.future, std::chrono::seconds(8));
    EXPECT_FALSE(result.isError);
    EXPECT_EQ(result.asText(), "counted 3");

    QEventLoop tail;
    QTimer::singleShot(100, &tail, &QEventLoop::quit);
    tail.exec();

    EXPECT_GE(steps->size(), 1);
    if (!steps->isEmpty()) {
        EXPECT_EQ(steps->last().total, 3.0);
    }

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, CancelRequestAbortsOutstandingCall)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();
    McpServer server(serverTransport, McpServerConfig{});

    class SlowTool : public BaseTool
    {
    public:
        using BaseTool::BaseTool;
        QString id() const override { return "slow"; }
        QString displayName() const override { return "Slow"; }
        QString description() const override { return "Never completes"; }
        QJsonObject parametersSchema() const override
        {
            return QJsonObject{{"type", "object"}};
        }
        QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &) override
        {
            auto p = std::make_shared<QPromise<LLMQore::ToolResult>>();
            p->start();
            m_held = p;
            return p->future();
        }
        std::shared_ptr<QPromise<LLMQore::ToolResult>> m_held;
    };
    auto *slow = new SlowTool(&server);
    server.addTool(slow);

    McpClient client(clientTransport);
    server.start();
    waitForFuture(client.connectAndInitialize());

    auto call = client.callToolWithProgress("slow", QJsonObject{});

    QEventLoop pump;
    QTimer::singleShot(50, &pump, &QEventLoop::quit);
    pump.exec();

    client.cancel(call.requestId, "test cancel");

    QEventLoop loop;
    QFutureWatcher<LLMQore::ToolResult> watcher;
    QObject::connect(&watcher, &QFutureWatcher<LLMQore::ToolResult>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(call.future);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    bool threw = false;
    try {
        (void) call.future.result();
    } catch (const Rpc::CancelledError &) {
        threw = true;
    } catch (const Rpc::JsonRpcException &) {
        threw = true;
    } catch (...) {
        threw = true;
    }
    EXPECT_TRUE(threw);

    slow->m_held->addResult(LLMQore::ToolResult::text("late"));
    slow->m_held->finish();

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, ResourcesListAndRead)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});
    auto *provider = new MemoryResourceProvider(&server);
    provider->addResource("mem://greeting", "hello world");
    server.addResourceProvider(provider);

    McpClient client(clientTransport);
    server.start();

    waitForFuture(client.connectAndInitialize());

    const auto resources = waitForFuture(client.listResources());
    ASSERT_EQ(resources.size(), 1);
    EXPECT_EQ(resources.first().uri, "mem://greeting");

    const auto contents = waitForFuture(client.readResource("mem://greeting"));
    EXPECT_EQ(contents.text, "hello world");

    delete serverTransport;
    delete clientTransport;
}

namespace {

QJsonObject passthroughBuilder(const CreateMessageParams &p)
{
    return p.toJson();
}

} // namespace

TEST_F(McpLoopbackTest, SamplingRoundTripsFromServerToClient)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});

    McpClient client(clientTransport);
    FakeSamplingClient fakeClient;
    fakeClient.cannedText = "pong";
    fakeClient.cannedStopReason = "end_turn";
    client.setSamplingClient(&fakeClient, &passthroughBuilder);

    server.start();
    waitForFuture(client.connectAndInitialize());

    CreateMessageParams params;
    SamplingMessage msg;
    msg.role = "user";
    msg.content = QJsonObject{{"type", "text"}, {"text", "ping"}};
    params.messages = {msg};
    params.maxTokens = 32;
    params.systemPrompt = "be terse";

    const CreateMessageResult result = waitForFuture(
        server.createSamplingMessage(params, std::chrono::seconds(5)));

    EXPECT_EQ(result.role, "assistant");
    EXPECT_EQ(result.content.value("type").toString(), "text");
    EXPECT_EQ(result.content.value("text").toString(), "pong");
    EXPECT_EQ(result.model, "fake-model-1.0");
    EXPECT_EQ(result.stopReason, "end_turn");

    EXPECT_TRUE(fakeClient.lastPayload.contains("messages"));
    const QJsonArray outMessages
        = fakeClient.lastPayload.value("messages").toArray();
    ASSERT_EQ(outMessages.size(), 1);
    EXPECT_EQ(
        outMessages.first().toObject().value("content").toObject().value("text").toString(),
        "ping");
    EXPECT_EQ(fakeClient.lastPayload.value("systemPrompt").toString(), "be terse");
    EXPECT_EQ(fakeClient.lastPayload.value("maxTokens").toInt(), 32);

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, SamplingWithoutClientFailsCapabilityGuard)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});
    McpClient client(clientTransport);

    server.start();
    waitForFuture(client.connectAndInitialize());

    CreateMessageParams params;
    SamplingMessage msg;
    msg.role = "user";
    msg.content = QJsonObject{{"type", "text"}, {"text", "hello"}};
    params.messages = {msg};
    params.maxTokens = 16;

    try {
        waitForFuture(server.createSamplingMessage(params, std::chrono::seconds(2)));
        FAIL() << "Expected Rpc::ProtocolError";
    } catch (const Rpc::ProtocolError &) {
        SUCCEED();
    } catch (const std::exception &e) {
        FAIL() << "Wrong exception type: " << e.what();
    }

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, SamplingClientErrorPropagatesAsRemoteError)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});

    McpClient client(clientTransport);
    FakeSamplingClient fakeClient;
    fakeClient.cannedError = "Upstream LLM refused";
    client.setSamplingClient(&fakeClient, &passthroughBuilder);

    server.start();
    waitForFuture(client.connectAndInitialize());

    CreateMessageParams params;
    SamplingMessage msg;
    msg.role = "user";
    msg.content = QJsonObject{{"type", "text"}, {"text", "hi"}};
    params.messages = {msg};
    params.maxTokens = 16;

    try {
        waitForFuture(server.createSamplingMessage(params, std::chrono::seconds(5)));
        FAIL() << "Expected Rpc::RemoteError";
    } catch (const Rpc::RemoteError &e) {
        EXPECT_EQ(e.code(), Rpc::ErrorCode::InternalError);
        EXPECT_EQ(e.remoteMessage(), "Upstream LLM refused");
    } catch (const std::exception &e) {
        FAIL() << "Wrong exception type: " << e.what();
    }

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, ElicitationRoundTripsFromServerToClient)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});

    McpClient client(clientTransport);
    CannedElicitationProvider provider;
    provider.cannedContent = QJsonObject{{"username", "alice"}, {"remember", true}};
    client.setElicitationProvider(&provider);

    server.start();
    waitForFuture(client.connectAndInitialize());

    ElicitRequestParams params;
    params.message = "Enter your username";
    params.requestedSchema = QJsonObject{
        {"type", "object"},
        {"properties",
         QJsonObject{
             {"username", QJsonObject{{"type", "string"}}},
             {"remember", QJsonObject{{"type", "boolean"}}},
         }},
        {"required", QJsonArray{"username"}},
    };

    const ElicitResult result = waitForFuture(
        server.createElicitation(params, std::chrono::seconds(5)));

    EXPECT_EQ(result.action, "accept");
    EXPECT_EQ(result.content.value("username").toString(), "alice");
    EXPECT_EQ(result.content.value("remember").toBool(), true);

    EXPECT_EQ(provider.lastParams.message, "Enter your username");
    EXPECT_EQ(
        provider.lastParams.requestedSchema.value("type").toString(), "object");
    EXPECT_TRUE(
        provider.lastParams.requestedSchema.value("properties")
            .toObject()
            .contains("username"));

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, ElicitationWithoutClientProviderFailsCapabilityGuard)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});
    McpClient client(clientTransport);

    server.start();
    waitForFuture(client.connectAndInitialize());

    ElicitRequestParams params;
    params.message = "hi";
    params.requestedSchema = QJsonObject{{"type", "object"}};

    try {
        waitForFuture(server.createElicitation(params, std::chrono::seconds(2)));
        FAIL() << "Expected Rpc::ProtocolError";
    } catch (const Rpc::ProtocolError &) {
        SUCCEED();
    } catch (const std::exception &e) {
        FAIL() << "Wrong exception type: " << e.what();
    }

    delete serverTransport;
    delete clientTransport;
}

TEST_F(McpLoopbackTest, ElicitationProviderRefusalPropagatesAsRemoteError)
{
    auto [serverTransport, clientTransport] = Rpc::PipeTransport::createPair();

    McpServer server(serverTransport, McpServerConfig{});

    McpClient client(clientTransport);
    RefusingElicitationProvider provider;
    client.setElicitationProvider(&provider);

    server.start();
    waitForFuture(client.connectAndInitialize());

    ElicitRequestParams params;
    params.message = "Enter value";
    params.requestedSchema = QJsonObject{{"type", "object"}};

    try {
        waitForFuture(server.createElicitation(params, std::chrono::seconds(5)));
        FAIL() << "Expected Rpc::RemoteError";
    } catch (const Rpc::RemoteError &e) {
        EXPECT_EQ(e.code(), Rpc::ErrorCode::InvalidParams);
        EXPECT_EQ(e.remoteMessage(), "Denied by user");
    } catch (const std::exception &e) {
        FAIL() << "Wrong exception type: " << e.what();
    }

    delete serverTransport;
    delete clientTransport;
}

#include "tst_McpLoopback.moc"

TEST_F(McpLoopbackTest, SubscribingToAResourceDeliversUpdatesUntilUnsubscribed)
{
    LLMQoreTest::McpPair pair;
    auto *provider = new MemoryResourceProvider(pair.server);
    provider->setSupportsSubscription(true);
    provider->addResource("mem://note", "first");
    pair.server->addResourceProvider(provider);

    const InitializeResult init = pair.initialize();
    ASSERT_TRUE(init.capabilities.resources.has_value());
    EXPECT_TRUE(init.capabilities.resources->subscribe)
        << "a provider that supports subscription must be advertised as such";

    QStringList updates;
    QObject::connect(pair.client, &McpClient::resourceUpdated, [&updates](const QString &uri) {
        updates << uri;
    });

    waitForVoidFuture(pair.client->subscribeResource("mem://note"));
    EXPECT_TRUE(provider->isSubscribed("mem://note"));

    provider->touch("mem://note", "second");
    pumpEventLoop(std::chrono::milliseconds(50));
    EXPECT_EQ(updates, QStringList{"mem://note"});

    waitForVoidFuture(pair.client->unsubscribeResource("mem://note"));
    EXPECT_FALSE(provider->isSubscribed("mem://note"));

    provider->touch("mem://note", "third");
    pumpEventLoop(std::chrono::milliseconds(50));
    EXPECT_EQ(updates, QStringList{"mem://note"})
        << "an unsubscribed client must stop hearing about the resource";
}

TEST_F(McpLoopbackTest, SubscriptionIsNotAdvertisedWhenNoProviderSupportsIt)
{
    LLMQoreTest::McpPair pair;
    auto *provider = new MemoryResourceProvider(pair.server);
    provider->addResource("mem://note", "first");
    pair.server->addResourceProvider(provider);

    const InitializeResult init = pair.initialize();
    ASSERT_TRUE(init.capabilities.resources.has_value());
    EXPECT_FALSE(init.capabilities.resources->subscribe);
}
