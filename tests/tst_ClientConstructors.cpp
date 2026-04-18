// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/LlamaCppClient.hpp>
#include <LLMQore/MistralClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>

using namespace LLMQore;

TEST(ClaudeClientConstructor, Basic)
{
    ClaudeClient client("https://api.anthropic.com", "sk-test", "claude-sonnet-4-6");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Claude);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(ClaudeClientConstructor, Default)
{
    ClaudeClient client;

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Claude);
    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(OpenAIClientConstructor, Basic)
{
    OpenAIClient client("https://api.openai.com/v1", "sk-test", "gpt-4");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAI);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(OpenAIClientConstructor, Default)
{
    OpenAIClient client;

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAI);
    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(OpenAIResponsesClientConstructor, Basic)
{
    OpenAIResponsesClient client("https://api.openai.com/v1", "sk-test", "o3-mini");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAIResponses);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(OpenAIResponsesClientConstructor, Default)
{
    OpenAIResponsesClient client;

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAIResponses);
    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(OllamaClientConstructor, Basic)
{
    OllamaClient client("http://localhost:11434", "", "llama3");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Ollama);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(OllamaClientConstructor, Default)
{
    OllamaClient client;

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Ollama);
    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(GoogleAIClientConstructor, Basic)
{
    GoogleAIClient
        client("https://generativelanguage.googleapis.com", "AIza-test", "gemini-2.5-flash");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Google);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(GoogleAIClientConstructor, Default)
{
    GoogleAIClient client;

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Google);
    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(LlamaCppClientConstructor, Basic)
{
    LlamaCppClient client("http://localhost:8080", "", "my-model");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAI);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(LlamaCppClientConstructor, Default)
{
    LlamaCppClient client;

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAI);
    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}

TEST(MistralClientConstructor, Basic)
{
    MistralClient client("https://api.mistral.ai/v1", "sk-test", "codestral-latest");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAI);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(MistralClientConstructor, Default)
{
    MistralClient client;

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAI);
    EXPECT_NE(client.tools(), nullptr);
    EXPECT_TRUE(client.url().isEmpty());
    EXPECT_TRUE(client.apiKey().isEmpty());
    EXPECT_TRUE(client.model().isEmpty());
}
