// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QSignalSpy>
#include <QtConcurrent/QtConcurrent>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/ToolResult.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "clients/claude/ClaudeMessage.hpp"
#include "clients/google/GoogleMessage.hpp"
#include "clients/ollama/OllamaMessage.hpp"
#include "clients/openai/OpenAIMessage.hpp"
#include "clients/openai/OpenAIResponsesMessage.hpp"

using namespace LLMQore;

class FakeTool : public BaseTool
{
    Q_OBJECT
public:
    FakeTool(const QString &id, const QString &displayName, QObject *parent = nullptr)
        : BaseTool(parent)
        , m_id(id)
        , m_displayName(displayName)
    {}

    QString id() const override { return m_id; }
    QString displayName() const override { return m_displayName; }
    QString description() const override { return "A fake tool for testing"; }

    QJsonObject parametersSchema() const override
    {
        return m_schema.isEmpty() ? QJsonObject{{"type", "object"}} : m_schema;
    }

    void setParametersSchema(const QJsonObject &schema) { m_schema = schema; }

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        Q_UNUSED(input)
        return QtConcurrent::run([]() -> ToolResult { return ToolResult::text("fake result"); });
    }

private:
    QString m_id;
    QString m_displayName;
    QJsonObject m_schema;
};

class ToolsManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_ToolsManager";
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

TEST_F(ToolsManagerTest, AddAndRetrieveTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    auto tool = new FakeTool("read_file", "Read File");
    mgr.addTool(tool);

    EXPECT_EQ(mgr.tool("read_file"), tool);
    EXPECT_EQ(mgr.registeredTools().size(), 1);
}

TEST_F(ToolsManagerTest, ToolsSnapshotDetachesFromLiveTools)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new FakeTool("read_file", "Read File"));

    const QList<ToolSnapshot> snapshot = mgr.toolsSnapshot();
    ASSERT_EQ(snapshot.size(), 1);
    EXPECT_EQ(snapshot.first().id, "read_file");
    EXPECT_EQ(snapshot.first().displayName, "Read File");

    mgr.removeTool("read_file");
    EXPECT_EQ(mgr.registeredTools().size(), 0);

    EXPECT_EQ(snapshot.first().id, "read_file");
    EXPECT_EQ(snapshot.first().displayName, "Read File");
    EXPECT_EQ(snapshot.first().description, "A fake tool for testing");
}

TEST_F(ToolsManagerTest, AddNullTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(nullptr);
    EXPECT_EQ(mgr.registeredTools().size(), 0);
}

TEST_F(ToolsManagerTest, RemoveTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new FakeTool("t1", "T1"));
    EXPECT_NE(mgr.tool("t1"), nullptr);

    mgr.removeTool("t1");
    EXPECT_EQ(mgr.tool("t1"), nullptr);
    EXPECT_EQ(mgr.registeredTools().size(), 0);
}

TEST_F(ToolsManagerTest, RemoveNonexistentTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.removeTool("nonexistent");
    EXPECT_EQ(mgr.registeredTools().size(), 0);
}

TEST_F(ToolsManagerTest, ReplaceExistingTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new FakeTool("t1", "Original"));
    auto *replacement = new FakeTool("t1", "Replacement");
    mgr.addTool(replacement);

    EXPECT_EQ(mgr.tool("t1"), replacement);
    EXPECT_EQ(mgr.tool("t1")->displayName(), "Replacement");
}

TEST_F(ToolsManagerTest, DisplayName_Known)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new FakeTool("t1", "My Tool"));
    EXPECT_EQ(mgr.displayName("t1"), "My Tool");
}

TEST_F(ToolsManagerTest, DisplayName_Unknown)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    EXPECT_EQ(mgr.displayName("nonexistent"), "Unknown tool");
}

TEST_F(ToolsManagerTest, ToolLookup_NotFound)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    EXPECT_EQ(mgr.tool("missing"), nullptr);
}

TEST_F(ToolsManagerTest, GetToolsDefinitions_AllFormats)
{
    auto makeTool = []() { return new FakeTool("t1", "T1"); };

    ToolsManager claudeMgr(ClaudeMessage::toolDialect());
    claudeMgr.addTool(makeTool());
    QJsonArray claudeDefs = claudeMgr.getToolsDefinitions();
    EXPECT_EQ(claudeDefs.size(), 1);
    EXPECT_EQ(claudeDefs[0].toObject()["name"].toString(), "t1");
    EXPECT_TRUE(claudeDefs[0].toObject().contains("input_schema"));

    ToolsManager openaiMgr(OpenAIMessage::toolDialect());
    openaiMgr.addTool(makeTool());
    QJsonArray openaiDefs = openaiMgr.getToolsDefinitions();
    EXPECT_EQ(openaiDefs.size(), 1);
    EXPECT_EQ(openaiDefs[0].toObject()["type"].toString(), "function");

    ToolsManager googleMgr(GoogleMessage::toolDialect());
    googleMgr.addTool(makeTool());
    QJsonArray googleDefs = googleMgr.getToolsDefinitions();
    EXPECT_EQ(googleDefs.size(), 1);
    EXPECT_TRUE(googleDefs[0].toObject().contains("function_declarations"));

    ToolsManager ollamaMgr(OllamaMessage::toolDialect());
    ollamaMgr.addTool(makeTool());
    QJsonArray ollamaDefs = ollamaMgr.getToolsDefinitions();
    ASSERT_EQ(ollamaDefs.size(), 1);
    const QJsonObject ollamaDef = ollamaDefs[0].toObject();
    EXPECT_EQ(ollamaDef["type"].toString(), "function");
    EXPECT_EQ(ollamaDef["function"].toObject()["name"].toString(), "t1");
    EXPECT_TRUE(ollamaDef["function"].toObject().contains("parameters"));

    ToolsManager responsesMgr(OpenAIResponsesMessage::toolDialect());
    responsesMgr.addTool(makeTool());
    QJsonArray responsesDefs = responsesMgr.getToolsDefinitions();
    ASSERT_EQ(responsesDefs.size(), 1);
    const QJsonObject responsesDef = responsesDefs[0].toObject();
    EXPECT_EQ(responsesDef["type"].toString(), "function");
    EXPECT_EQ(responsesDef["name"].toString(), "t1")
        << "the Responses shape is flat -- no nested \"function\" object";
    EXPECT_FALSE(responsesDef.contains("function"));
    EXPECT_TRUE(responsesDef.contains("parameters"));
}

TEST_F(ToolsManagerTest, GetToolsDefinitions_GoogleEnvelopeIsOmittedWhenEmpty)
{
    ToolsManager googleMgr(GoogleMessage::toolDialect());
    EXPECT_TRUE(googleMgr.getToolsDefinitions().isEmpty());
}

TEST_F(ToolsManagerTest, GetToolsDefinitions_GoogleStripsUnsupportedSchemaKeys)
{
    // Simulates an MCP-backed tool whose schema comes from a remote server using
    // JSON Schema draft-07, which includes "$schema" and other meta keys Gemini rejects.
    QJsonObject nestedItemSchema{
        {"type", "object"},
        {"$schema", "http://json-schema.org/draft-07/schema#"},
        {"additionalProperties", false},
        {"properties", QJsonObject{{"name", QJsonObject{{"type", "string"}}}}},
    };
    QJsonObject dirtySchema{
        {"$schema", "http://json-schema.org/draft-07/schema#"},
        {"$id", "https://example.com/tool.json"},
        {"type", "object"},
        {"additionalProperties", false},
        {"definitions", QJsonObject{{"Foo", QJsonObject{{"type", "string"}}}}},
        {"properties",
         QJsonObject{
             {"path", QJsonObject{{"type", "string"}}},
             {"items",
              QJsonObject{
                  {"type", "array"},
                  {"items", nestedItemSchema},
              }},
         }},
        {"required", QJsonArray{"path"}},
    };

    auto *googleTool = new FakeTool("read_file", "Read File");
    googleTool->setParametersSchema(dirtySchema);

    ToolsManager googleMgr(GoogleMessage::toolDialect());
    googleMgr.addTool(googleTool);
    QJsonArray googleDefs = googleMgr.getToolsDefinitions();
    ASSERT_EQ(googleDefs.size(), 1);

    QJsonObject wrapper = googleDefs[0].toObject();
    ASSERT_TRUE(wrapper.contains("function_declarations"));
    QJsonArray decls = wrapper["function_declarations"].toArray();
    ASSERT_EQ(decls.size(), 1);

    QJsonObject params = decls[0].toObject()["parameters"].toObject();
    EXPECT_FALSE(params.contains("$schema"));
    EXPECT_FALSE(params.contains("$id"));
    EXPECT_FALSE(params.contains("additionalProperties"));
    EXPECT_FALSE(params.contains("definitions"));
    EXPECT_EQ(params["type"].toString(), "object");
    EXPECT_TRUE(params.contains("properties"));

    // Recurses into nested items.
    QJsonObject items = params["properties"].toObject()["items"].toObject()["items"].toObject();
    EXPECT_FALSE(items.contains("$schema"));
    EXPECT_FALSE(items.contains("additionalProperties"));
    EXPECT_EQ(items["type"].toString(), "object");

    // Claude format must keep the schema untouched (its API accepts meta keys).
    auto *claudeTool = new FakeTool("read_file", "Read File");
    claudeTool->setParametersSchema(dirtySchema);
    ToolsManager claudeMgr(ClaudeMessage::toolDialect());
    claudeMgr.addTool(claudeTool);
    QJsonObject claudeSchema
        = claudeMgr.getToolsDefinitions()[0].toObject()["input_schema"].toObject();
    EXPECT_TRUE(claudeSchema.contains("$schema"));
    EXPECT_TRUE(claudeSchema.contains("additionalProperties"));
}

TEST_F(ToolsManagerTest, GetToolsDefinitions_DisabledToolExcluded)
{
    ToolsManager mgr(ClaudeMessage::toolDialect());
    auto tool = new FakeTool("t1", "T1");
    tool->setEnabled(false);
    mgr.addTool(tool);

    QJsonArray defs = mgr.getToolsDefinitions();
    EXPECT_EQ(defs.size(), 0);
}

TEST_F(ToolsManagerTest, ExecuteToolCall_UnknownTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    QSignalSpy completeSpy(&mgr, &ToolsManager::toolExecutionComplete);

    mgr.executeToolCall("req-1", "tool-1", "nonexistent", {});

    // Signal fires synchronously for unknown tools
    EXPECT_EQ(completeSpy.count(), 1);
    auto results = completeSpy[0][1].value<QHash<QString, ToolResult>>();
    EXPECT_TRUE(results["tool-1"].isError);
    EXPECT_TRUE(results["tool-1"].asText().contains("Error"));
}

TEST_F(ToolsManagerTest, ExecuteToolCall_Success)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new FakeTool("fake", "Fake"));

    QSignalSpy startSpy(&mgr, &ToolsManager::toolExecutionStarted);
    QSignalSpy resultSpy(&mgr, &ToolsManager::toolExecutionResult);
    QSignalSpy completeSpy(&mgr, &ToolsManager::toolExecutionComplete);

    mgr.executeToolCall("req-1", "tool-1", "fake", {});

    EXPECT_TRUE(completeSpy.wait(3000));
    EXPECT_EQ(startSpy.count(), 1);
    EXPECT_EQ(resultSpy.count(), 1);

    auto results = completeSpy[0][1].value<QHash<QString, ToolResult>>();
    EXPECT_EQ(results["tool-1"].asText(), "fake result");
    EXPECT_FALSE(results["tool-1"].isError);
}

TEST_F(ToolsManagerTest, CleanupRequest)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new FakeTool("fake", "Fake"));
    QSignalSpy completeSpy(&mgr, &ToolsManager::toolExecutionComplete);

    mgr.executeToolCall("req-1", "tool-1", "fake", {});
    EXPECT_TRUE(completeSpy.wait(3000));

    mgr.cleanupRequest("req-1");
    mgr.cleanupRequest("req-nonexistent");
}


class BlockingTool : public BaseTool
{
    Q_OBJECT
public:
    using BaseTool::BaseTool;

    QString id() const override { return QStringLiteral("blocker"); }
    QString displayName() const override { return QStringLiteral("Blocker"); }
    QString description() const override { return QStringLiteral("Finishes on demand"); }
    QJsonObject parametersSchema() const override { return QJsonObject{{"type", "object"}}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        auto promise = std::make_shared<QPromise<ToolResult>>();
        promise->start();
        pending.append(promise);
        return promise->future();
    }

    void release(int index, const QString &text)
    {
        pending[index]->addResult(ToolResult::text(text));
        pending[index]->finish();
    }

    QList<std::shared_ptr<QPromise<ToolResult>>> pending;
};

namespace {

void pumpEvents(int rounds = 20)
{
    for (int i = 0; i < rounds; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

TEST_F(ToolsManagerTest, RoundClosesOnceWhenAnUnknownToolPrecedesAValidOne)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    auto *blocker = new BlockingTool;
    mgr.addTool(blocker);

    QSignalSpy complete(&mgr, &ToolsManager::toolExecutionComplete);

    mgr.beginRound(
        "req-1",
        {ToolRound::Call{"call_1", "no_such_tool", {}}, ToolRound::Call{"call_2", "blocker", {}}});

    EXPECT_EQ(complete.count(), 0)
        << "the round must not close while one of its calls is still running";
    ASSERT_EQ(blocker->pending.size(), 1);

    blocker->release(0, QStringLiteral("done"));
    ASSERT_TRUE(complete.count() > 0 || complete.wait(3000));

    ASSERT_EQ(complete.count(), 1);
    const auto results = complete.first().at(1).value<LLMQoreToolResultHash>();
    EXPECT_EQ(results.size(), 2);
    EXPECT_TRUE(results["call_1"].isError);
    EXPECT_EQ(results["call_2"].asText(), QStringLiteral("done"));
}

TEST_F(ToolsManagerTest, CleanupRequestOnALiveRoundKeepsItFromClosing)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    auto *blocker = new BlockingTool;
    mgr.addTool(blocker);

    QSignalSpy complete(&mgr, &ToolsManager::toolExecutionComplete);

    mgr.beginRound("req-1", {ToolRound::Call{"call_1", "blocker", {}}, ToolRound::Call{"call_2", "blocker", {}}});
    ASSERT_EQ(blocker->pending.size(), 1);
    ASSERT_EQ(complete.count(), 0);

    mgr.cleanupRequest("req-1");

    blocker->release(0, QStringLiteral("too late"));
    pumpEvents();

    EXPECT_EQ(complete.count(), 0) << "an abandoned round must never close";
    EXPECT_EQ(blocker->pending.size(), 1) << "the second call must never start";
}

TEST_F(ToolsManagerTest, BaseTool_EnableDisable)
{
    FakeTool tool("t", "T");
    EXPECT_TRUE(tool.isEnabled());

    tool.setEnabled(false);
    EXPECT_FALSE(tool.isEnabled());

    tool.setEnabled(true);
    EXPECT_TRUE(tool.isEnabled());
}


// --- structuredContent used to be dropped by every continuation builder ---

namespace {

ToolResult structuredResult()
{
    ToolResult r;
    r.content = {TextContent{"rendered"}};
    r.structuredContent = QJsonObject{{"celsius", 21}};
    return r;
}

void callWeather(OpenAIMessage &msg)
{
    const QJsonObject call{
        {"index", 0},
        {"id", "call_1"},
        {"function", QJsonObject{{"name", "weather"}, {"arguments", "{}"}}}};
    msg.applyEvent(QJsonObject{
        {"choices",
         QJsonArray{QJsonObject{
             {"delta", QJsonObject{{"tool_calls", QJsonArray{call}}}},
             {"finish_reason", "tool_calls"}}}}});
}

} // namespace

TEST(ToolResultText, CarriesStructuredContentAlongsideTheRenderedText)
{
    OpenAIMessage msg;
    callWeather(msg);

    const QJsonArray messages = msg.createToolResultMessages({{"call_1", structuredResult()}});
    ASSERT_EQ(messages.size(), 1);

    const QString content = messages.first().toObject().value("content").toString();
    EXPECT_TRUE(content.contains(QStringLiteral("rendered")));
    EXPECT_TRUE(content.contains(QStringLiteral("\"celsius\":21")))
        << "a tool's machine-readable output never reached the model: " << qPrintable(content);
}

TEST(ToolResultText, PlainResultIsUnchanged)
{
    OpenAIMessage msg;
    callWeather(msg);

    const QJsonArray messages
        = msg.createToolResultMessages({{"call_1", ToolResult::text("just text")}});
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages.first().toObject().value("content").toString(),
              QStringLiteral("just text"));
}

TEST(ToolResultText, HasOnlyTextIsFalseWhenABlockIsBinary)
{
    ToolResult r;
    r.content = {TextContent{"a"}, ImageContent::fromBytes("bytes", "image/png")};
    EXPECT_FALSE(r.hasOnlyText());

    EXPECT_TRUE(ToolResult::text("a").hasOnlyText());
    EXPECT_TRUE(ToolResult::empty().hasOnlyText());
}

// --- ExecutionGate: declared and wired, but had no coverage ---

class SafetyTool : public FakeTool
{
    Q_OBJECT
public:
    SafetyTool(const QString &id, ToolSafety safety, QObject *parent = nullptr)
        : FakeTool(id, id, parent)
        , m_safety(safety)
    {}

    ToolSafety safety() const override { return m_safety; }

private:
    ToolSafety m_safety;
};

namespace {

QFuture<bool> answer(bool value)
{
    QPromise<bool> promise;
    QFuture<bool> future = promise.future();
    promise.start();
    promise.addResult(value);
    promise.finish();
    return future;
}

bool waitForCompletion(QSignalSpy &spy, int timeoutMs = 10000)
{
    return spy.count() > 0 || spy.wait(timeoutMs);
}

} // namespace

TEST_F(ToolsManagerTest, ExecutionGateDeclinesAMutatingTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new SafetyTool("writer", ToolSafety::Mutating));

    QStringList asked;
    mgr.setExecutionGate([&asked](const QString &, const QString &, const QString &name,
                                  const QJsonObject &) {
        asked << name;
        return answer(false);
    });

    QSignalSpy complete(&mgr, &ToolsManager::toolExecutionComplete);
    mgr.executeToolCall("req", "call_1", "writer", QJsonObject{});
    ASSERT_TRUE(waitForCompletion(complete)) << "toolExecutionComplete never arrived";

    ASSERT_EQ(asked, QStringList{"writer"});
    ASSERT_EQ(complete.count(), 1);

    const auto results = complete.first().at(1).value<LLMQoreToolResultHash>();
    ASSERT_TRUE(results.contains("call_1"));
    EXPECT_TRUE(results["call_1"].asText().contains("declined"));
}

TEST_F(ToolsManagerTest, ExecutionGateAllowsAMutatingTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new SafetyTool("writer", ToolSafety::Mutating));

    mgr.setExecutionGate([](const QString &, const QString &, const QString &,
                            const QJsonObject &) { return answer(true); });

    QSignalSpy complete(&mgr, &ToolsManager::toolExecutionComplete);
    mgr.executeToolCall("req", "call_1", "writer", QJsonObject{});
    ASSERT_TRUE(waitForCompletion(complete)) << "toolExecutionComplete never arrived";

    ASSERT_EQ(complete.count(), 1);
    const auto results = complete.first().at(1).value<LLMQoreToolResultHash>();
    EXPECT_EQ(results["call_1"].asText(), QStringLiteral("fake result"));
}

TEST_F(ToolsManagerTest, ExecutionGateIsNotConsultedForAReadOnlyTool)
{
    ToolsManager mgr(OpenAIMessage::toolDialect());
    mgr.addTool(new SafetyTool("reader", ToolSafety::ReadOnly));

    bool asked = false;
    mgr.setExecutionGate([&asked](const QString &, const QString &, const QString &,
                                  const QJsonObject &) {
        asked = true;
        return answer(false);
    });

    QSignalSpy complete(&mgr, &ToolsManager::toolExecutionComplete);
    mgr.executeToolCall("req", "call_1", "reader", QJsonObject{});
    ASSERT_TRUE(waitForCompletion(complete)) << "toolExecutionComplete never arrived";

    EXPECT_FALSE(asked) << "a read-only tool has nothing for the user to approve";
    ASSERT_EQ(complete.count(), 1);
    EXPECT_EQ(complete.first().at(1).value<LLMQoreToolResultHash>()["call_1"].asText(),
              QStringLiteral("fake result"));
}

#include "tst_ToolsManager.moc"
