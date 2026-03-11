// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <LLMCore/ClaudeClient.hpp>
#include <LLMCore/GoogleAIClient.hpp>
#include <LLMCore/LlamaCppClient.hpp>
#include <LLMCore/OllamaClient.hpp>
#include <LLMCore/OpenAIClient.hpp>
#include <LLMCore/OpenAIResponsesClient.hpp>

using namespace LLMCore;

TEST(ClaudeClientConstructor, Basic)
{
    ClaudeClient client("https://api.anthropic.com", "sk-test", "claude-sonnet-4-6");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Claude);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(OpenAIClientConstructor, Basic)
{
    OpenAIClient client("https://api.openai.com", "sk-test", "gpt-4");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAI);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(OpenAIResponsesClientConstructor, Basic)
{
    OpenAIResponsesClient client("https://api.openai.com", "sk-test", "o3-mini");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAIResponses);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(OllamaClientConstructor, Basic)
{
    OllamaClient client("http://localhost:11434", "", "llama3");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Ollama);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(GoogleAIClientConstructor, Basic)
{
    GoogleAIClient
        client("https://generativelanguage.googleapis.com", "AIza-test", "gemini-2.5-flash");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::Google);
    EXPECT_NE(client.tools(), nullptr);
}

TEST(LlamaCppClientConstructor, Basic)
{
    LlamaCppClient client("http://localhost:8080", "", "my-model");

    EXPECT_EQ(client.toolSchemaFormat(), ToolSchemaFormat::OpenAI);
    EXPECT_NE(client.tools(), nullptr);
}
