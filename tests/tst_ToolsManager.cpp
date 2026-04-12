// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtConcurrent/QtConcurrent>

#include <LLMCore/BaseTool.hpp>
#include <LLMCore/ToolResult.hpp>
#include <LLMCore/ToolsManager.hpp>

using namespace LLMCore;

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

    QJsonObject parametersSchema() const override { return QJsonObject{{"type", "object"}}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        Q_UNUSED(input)
        return QtConcurrent::run([]() -> ToolResult { return ToolResult::text("fake result"); });
    }

private:
    QString m_id;
    QString m_displayName;
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
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    auto *tool = new FakeTool("read_file", "Read File");
    mgr.addTool(tool);

    EXPECT_EQ(mgr.tool("read_file"), tool);
    EXPECT_EQ(mgr.registeredTools().size(), 1);
}

TEST_F(ToolsManagerTest, AddNullTool)
{
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    mgr.addTool(nullptr);
    EXPECT_EQ(mgr.registeredTools().size(), 0);
}

TEST_F(ToolsManagerTest, RemoveTool)
{
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    mgr.addTool(new FakeTool("t1", "T1"));
    EXPECT_NE(mgr.tool("t1"), nullptr);

    mgr.removeTool("t1");
    EXPECT_EQ(mgr.tool("t1"), nullptr);
    EXPECT_EQ(mgr.registeredTools().size(), 0);
}

TEST_F(ToolsManagerTest, RemoveNonexistentTool)
{
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    mgr.removeTool("nonexistent");
    EXPECT_EQ(mgr.registeredTools().size(), 0);
}

TEST_F(ToolsManagerTest, ReplaceExistingTool)
{
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    mgr.addTool(new FakeTool("t1", "Original"));
    auto *replacement = new FakeTool("t1", "Replacement");
    mgr.addTool(replacement);

    EXPECT_EQ(mgr.tool("t1"), replacement);
    EXPECT_EQ(mgr.tool("t1")->displayName(), "Replacement");
}

TEST_F(ToolsManagerTest, DisplayName_Known)
{
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    mgr.addTool(new FakeTool("t1", "My Tool"));
    EXPECT_EQ(mgr.displayName("t1"), "My Tool");
}

TEST_F(ToolsManagerTest, DisplayName_Unknown)
{
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    EXPECT_EQ(mgr.displayName("nonexistent"), "Unknown tool");
}

TEST_F(ToolsManagerTest, ToolLookup_NotFound)
{
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    EXPECT_EQ(mgr.tool("missing"), nullptr);
}

TEST_F(ToolsManagerTest, GetToolsDefinitions_AllFormats)
{
    auto makeTool = []() { return new FakeTool("t1", "T1"); };

    ToolsManager claudeMgr(ToolSchemaFormat::Claude);
    claudeMgr.addTool(makeTool());
    QJsonArray claudeDefs = claudeMgr.getToolsDefinitions();
    EXPECT_EQ(claudeDefs.size(), 1);
    EXPECT_EQ(claudeDefs[0].toObject()["name"].toString(), "t1");
    EXPECT_TRUE(claudeDefs[0].toObject().contains("input_schema"));

    ToolsManager openaiMgr(ToolSchemaFormat::OpenAI);
    openaiMgr.addTool(makeTool());
    QJsonArray openaiDefs = openaiMgr.getToolsDefinitions();
    EXPECT_EQ(openaiDefs.size(), 1);
    EXPECT_EQ(openaiDefs[0].toObject()["type"].toString(), "function");

    ToolsManager googleMgr(ToolSchemaFormat::Google);
    googleMgr.addTool(makeTool());
    QJsonArray googleDefs = googleMgr.getToolsDefinitions();
    EXPECT_EQ(googleDefs.size(), 1);
    EXPECT_TRUE(googleDefs[0].toObject().contains("function_declarations"));
}

TEST_F(ToolsManagerTest, GetToolsDefinitions_DisabledToolExcluded)
{
    ToolsManager mgr(ToolSchemaFormat::Claude);
    auto *tool = new FakeTool("t1", "T1");
    tool->setEnabled(false);
    mgr.addTool(tool);

    QJsonArray defs = mgr.getToolsDefinitions();
    EXPECT_EQ(defs.size(), 0);
}

TEST_F(ToolsManagerTest, ExecuteToolCall_UnknownTool)
{
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
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
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
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
    ToolsManager mgr(ToolSchemaFormat::OpenAI);
    mgr.addTool(new FakeTool("fake", "Fake"));
    QSignalSpy completeSpy(&mgr, &ToolsManager::toolExecutionComplete);

    mgr.executeToolCall("req-1", "tool-1", "fake", {});
    EXPECT_TRUE(completeSpy.wait(3000));

    mgr.cleanupRequest("req-1");
    mgr.cleanupRequest("req-nonexistent");
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

#include "tst_ToolsManager.moc"
