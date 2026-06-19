// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <LLMQore/AcpAgentRegistry.hpp>

using namespace LLMQore::Acp;

namespace {

QJsonObject sampleCatalogue()
{
    return QJsonObject{
        {"agents",
         QJsonArray{
             QJsonObject{
                 {"id", "claude"},
                 {"name", "Claude Code"},
                 {"command", "npx"},
                 {"args", QJsonArray{"-y", "@agentclientprotocol/claude-agent-acp"}}},
             QJsonObject{
                 {"id", "gemini"},
                 {"name", "Gemini CLI"},
                 {"command", "gemini"},
                 {"args", QJsonArray{"--experimental-acp"}}},
         }}};
}

TEST(AcpAgentRegistry, LoadFromJsonAndLookup)
{
    AcpAgentRegistry reg;
    EXPECT_TRUE(reg.isEmpty());
    reg.loadFromJson(sampleCatalogue());

    EXPECT_EQ(reg.size(), 2);
    EXPECT_TRUE(reg.contains("claude"));
    EXPECT_TRUE(reg.contains("gemini"));
    EXPECT_FALSE(reg.contains("nope"));
    ASSERT_EQ(reg.ids().size(), 2);
    EXPECT_EQ(reg.ids().first(), "claude"); // insertion order preserved

    const auto entry = reg.entry("claude");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->name, "Claude Code");
    EXPECT_EQ(entry->config.command, "npx");
    EXPECT_TRUE(entry->config.args.contains("@agentclientprotocol/claude-agent-acp"));
}

TEST(AcpAgentRegistry, ConfigFillsCwd)
{
    AcpAgentRegistry reg;
    reg.loadFromJson(sampleCatalogue());

    const auto cfg = reg.config("gemini", "/work/project");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->command, "gemini");
    EXPECT_EQ(cfg->cwd, "/work/project");

    EXPECT_FALSE(reg.config("unknown").has_value());
}

TEST(AcpAgentRegistry, LoadMergesAndOverridesById)
{
    AcpAgentRegistry reg;
    reg.loadFromJson(sampleCatalogue());
    EXPECT_EQ(reg.size(), 2);

    // Override "gemini" and add a new one.
    reg.loadFromJson(QJsonObject{
        {"agents",
         QJsonArray{
             QJsonObject{
                 {"id", "gemini"}, {"name", "Gemini"}, {"command", "qwen"}},
             QJsonObject{{"id", "custom"}, {"command", "myacp"}},
         }}});

    EXPECT_EQ(reg.size(), 3); // gemini overridden, custom added
    EXPECT_EQ(reg.entry("gemini")->config.command, "qwen");
    EXPECT_EQ(reg.entry("custom")->name, "custom"); // name defaults to id
}

TEST(AcpAgentRegistry, LoadFromFile)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("agents.json");
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(sampleCatalogue()).toJson());
        f.close();
    }

    AcpAgentRegistry reg;
    EXPECT_TRUE(reg.loadFromFile(path));
    EXPECT_EQ(reg.size(), 2);

    EXPECT_FALSE(reg.loadFromFile(dir.filePath("missing.json")));
}

TEST(AcpAgentRegistry, JsonRoundTrip)
{
    AcpAgentRegistry reg;
    reg.loadFromJson(sampleCatalogue());

    AcpAgentRegistry back;
    back.loadFromJson(reg.toJson());
    EXPECT_EQ(back.size(), 2);
    EXPECT_EQ(back.entry("claude")->config.command, "npx");
}

} // namespace
