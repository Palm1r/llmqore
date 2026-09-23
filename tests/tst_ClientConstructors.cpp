// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/LlamaCppClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>

#include <QJsonArray>
#include <QJsonObject>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/ToolsManager.hpp>

using namespace LLMQore;

namespace {

class ProbeTool : public BaseTool
{
    Q_OBJECT
public:
    using BaseTool::BaseTool;

    QString id() const override { return QStringLiteral("probe"); }
    QString displayName() const override { return QStringLiteral("Probe"); }
    QString description() const override { return QStringLiteral("A probe"); }
    QJsonObject parametersSchema() const override { return QJsonObject{{"type", "object"}}; }
    QFuture<ToolResult> executeAsync(const QJsonObject &) override { return {}; }
};

// The dialect a client was wired with, read off the wire rather than off a tag.
QJsonObject soleDefinition(BaseClient &client)
{
    client.tools()->addTool(new ProbeTool(&client));
    const QJsonArray definitions = client.tools()->getToolsDefinitions();
    return definitions.size() == 1 ? definitions.first().toObject() : QJsonObject{};
}

} // namespace

TEST(ClaudeClientConstructor, Basic)
{
    ClaudeClient client("https://api.anthropic.com", "sk-test", "claude-sonnet-4-6");

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("input_schema"));
}

TEST(ClaudeClientConstructor, Default)
{
    ClaudeClient client;

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("input_schema"));
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(OpenAIClientConstructor, Basic)
{
    OpenAIClient client("https://api.openai.com/v1", "sk-test", "gpt-4");

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function"));
}

TEST(OpenAIClientConstructor, Default)
{
    OpenAIClient client;

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function"));
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(OpenAIResponsesClientConstructor, Basic)
{
    OpenAIResponsesClient client("https://api.openai.com/v1", "sk-test", "o3-mini");

    EXPECT_NE(client.tools(), nullptr);
    const QJsonObject responsesDefinition = soleDefinition(client);
    EXPECT_FALSE(responsesDefinition.contains("function"));
    EXPECT_EQ(responsesDefinition.value("name").toString(), QStringLiteral("probe"));
}

TEST(OpenAIResponsesClientConstructor, Default)
{
    OpenAIResponsesClient client;

    EXPECT_NE(client.tools(), nullptr);
    const QJsonObject responsesDefinition = soleDefinition(client);
    EXPECT_FALSE(responsesDefinition.contains("function"));
    EXPECT_EQ(responsesDefinition.value("name").toString(), QStringLiteral("probe"));
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(OllamaClientConstructor, Basic)
{
    OllamaClient client("http://localhost:11434", "", "llama3");

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function"));
}

TEST(OllamaClientConstructor, Default)
{
    OllamaClient client;

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function"));
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(GoogleAIClientConstructor, Basic)
{
    GoogleAIClient
        client("https://generativelanguage.googleapis.com", "AIza-test", "gemini-2.5-flash");

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function_declarations"));
}

TEST(GoogleAIClientConstructor, Default)
{
    GoogleAIClient client;

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function_declarations"));
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(LlamaCppClientConstructor, Basic)
{
    LlamaCppClient client("http://localhost:8080", "", "my-model");

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function"));
}

TEST(LlamaCppClientConstructor, Default)
{
    LlamaCppClient client;

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function"));
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(MistralProfile, IsAnOpenAIClientWithDifferentPaths)
{
    OpenAIClient client("https://api.mistral.ai", "sk-test", "codestral-latest");
    client.setProfile(mistralProfile());

    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(soleDefinition(client).contains("function"));
    EXPECT_EQ(client.profile().chatPath, QStringLiteral("/v1/chat/completions"));
    EXPECT_EQ(client.profile().modelsPath, QStringLiteral("/v1/models"));
}

TEST(ClientConstructors, QtStyleParentOverloadsAdoptTheParent)
{
    QObject owner;
    const QList<BaseClient *> clients{
        new ClaudeClient("https://api.anthropic.com", "sk-test", "claude-sonnet-4-5", &owner),
        new OpenAIClient("https://api.openai.com/v1", "sk-test", "gpt-test", &owner),
        new OpenAIResponsesClient("https://api.openai.com/v1", "sk-test", "gpt-test", &owner),
        new GoogleAIClient("https://generativelanguage.googleapis.com", "key", "gemini", &owner),
        new OllamaClient("https://ollama.local", {}, "llama3", &owner),
        new LlamaCppClient("https://llama.local", {}, {}, &owner),
        new ClaudeClient(&owner),
        new OpenAIClient(&owner),
        new OpenAIResponsesClient(&owner),
        new GoogleAIClient(&owner),
        new OllamaClient(&owner),
        new LlamaCppClient(&owner)};

    for (BaseClient *client : clients)
        EXPECT_EQ(client->parent(), &owner);

    EXPECT_EQ(clients.first()->url(), QStringLiteral("https://api.anthropic.com"));
    EXPECT_EQ(clients.first()->model(), QStringLiteral("claude-sonnet-4-5"));
}

#include "tst_ClientConstructors.moc"
