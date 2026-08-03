// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QUrlQuery>

#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/LlamaCppClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>

#include "FakeHttpTransport.hpp"

using namespace LLMQore;
using LLMQoreTest::FakeHttpTransport;
using LLMQoreTest::SentRequest;

namespace {

using ClientFactory = std::function<BaseClient *(const QString &, HttpTransport *)>;

struct ProviderCase
{
    const char *name;
    ClientFactory make;
    QByteArray authHeader;
    QByteArray authHeaderValue;
    QString authQueryItem;
    QList<QPair<QByteArray, QByteArray>> defaultHeaders;
};

template<typename T>
ClientFactory factoryFor()
{
    return [](const QString &apiKey, HttpTransport *transport) -> BaseClient * {
        return new T(
            QStringLiteral("http://fake.local"), apiKey, QStringLiteral("m"), transport, nullptr);
    };
}

ClientFactory factoryForProfile(ProviderProfile (*profile)())
{
    return [profile](const QString &apiKey, HttpTransport *transport) -> BaseClient * {
        auto *client = new OpenAIClient(
            QStringLiteral("http://fake.local"), apiKey, QStringLiteral("m"), transport, nullptr);
        client->setProfile(profile());
        return client;
    };
}

const QList<QPair<QByteArray, QByteArray>> kJsonOnly = {{"Content-Type", "application/json"}};

std::vector<ProviderCase> providerCases()
{
    return {
        ProviderCase{
            "Claude",
            factoryFor<ClaudeClient>(),
            "x-api-key",
            "sk-test",
            {},
            {{"Content-Type", "application/json"}, {"anthropic-version", "2023-06-01"}}},
        ProviderCase{
            "OpenAI", factoryFor<OpenAIClient>(), "Authorization", "Bearer sk-test", {}, kJsonOnly},
        ProviderCase{
            "OpenAIResponses",
            factoryFor<OpenAIResponsesClient>(),
            "Authorization",
            "Bearer sk-test",
            {},
            kJsonOnly},
        ProviderCase{
            "MistralProfile",
            factoryForProfile(&mistralProfile),
            "Authorization",
            "Bearer sk-test",
            {},
            kJsonOnly},
        ProviderCase{
            "Ollama", factoryFor<OllamaClient>(), "Authorization", "Bearer sk-test", {}, kJsonOnly},
        ProviderCase{
            "LlamaCpp",
            factoryFor<LlamaCppClient>(),
            "Authorization",
            "Bearer sk-test",
            {},
            kJsonOnly},
        ProviderCase{
            "GoogleAI", factoryFor<GoogleAIClient>(), {}, {}, QStringLiteral("key"), kJsonOnly},
    };
}

bool authIsQueryParam(const ProviderCase &provider)
{
    return provider.authHeader.isEmpty();
}

class ProviderHeaders : public ::testing::TestWithParam<ProviderCase>
{
protected:
    SentRequest ask(BaseClient *client)
    {
        client->ask(QStringLiteral("hi"));
        EXPECT_EQ(transport.streamCount(), 1);
        return transport.streamRequest(0);
    }

    std::unique_ptr<BaseClient> makeClient(const QString &apiKey = QStringLiteral("sk-test"))
    {
        return std::unique_ptr<BaseClient>(GetParam().make(apiKey, &transport));
    }

    FakeHttpTransport transport;
};

} // namespace

TEST_P(ProviderHeaders, DefaultHeadersReachTheRequest)
{
    auto client = makeClient();
    const SentRequest sent = ask(client.get());

    for (const auto &header : GetParam().defaultHeaders)
        EXPECT_EQ(sent.header(header.first), header.second) << header.first.constData();
}

TEST_P(ProviderHeaders, ApiKeyGoesWhereTheSchemeSays)
{
    auto client = makeClient();
    const SentRequest sent = ask(client.get());

    if (authIsQueryParam(GetParam())) {
        EXPECT_EQ(
            QUrlQuery(sent.url()).queryItemValue(GetParam().authQueryItem),
            QStringLiteral("sk-test"));
    } else {
        EXPECT_EQ(sent.header(GetParam().authHeader), GetParam().authHeaderValue);
    }
}

TEST_P(ProviderHeaders, EmptyApiKeyOmitsAuth)
{
    auto client = makeClient(QString());
    const SentRequest sent = ask(client.get());

    if (authIsQueryParam(GetParam()))
        EXPECT_FALSE(QUrlQuery(sent.url()).hasQueryItem(GetParam().authQueryItem));
    else
        EXPECT_TRUE(sent.header(GetParam().authHeader).isEmpty());
}

TEST_P(ProviderHeaders, SetHeaderOverridesOneDefaultAndKeepsTheRest)
{
    auto client = makeClient();
    client->setHeader(QStringLiteral("Content-Type"), QStringLiteral("application/vnd.test"));
    const SentRequest sent = ask(client.get());

    EXPECT_EQ(sent.header("Content-Type"), QByteArray("application/vnd.test"));

    for (const auto &header : GetParam().defaultHeaders) {
        if (header.first != QByteArray("Content-Type"))
            EXPECT_EQ(sent.header(header.first), header.second) << header.first.constData();
    }
}

TEST_P(ProviderHeaders, SetHeaderAddsANewHeaderWithoutTouchingAuth)
{
    auto client = makeClient();
    client->setHeader(QStringLiteral("HTTP-Referer"), QStringLiteral("https://example.test"));
    const SentRequest sent = ask(client.get());

    EXPECT_EQ(sent.header("HTTP-Referer"), QByteArray("https://example.test"));

    if (authIsQueryParam(GetParam())) {
        EXPECT_EQ(
            QUrlQuery(sent.url()).queryItemValue(GetParam().authQueryItem),
            QStringLiteral("sk-test"));
    } else {
        EXPECT_EQ(sent.header(GetParam().authHeader), GetParam().authHeaderValue);
    }
}

TEST_P(ProviderHeaders, SetHeadersReplacesTheWholeMap)
{
    auto client = makeClient();
    client->setHeaders({{QStringLiteral("X-Only"), QStringLiteral("1")}});
    const SentRequest sent = ask(client.get());

    EXPECT_EQ(sent.header("X-Only"), QByteArray("1"));
    for (const auto &header : GetParam().defaultHeaders)
        EXPECT_TRUE(sent.header(header.first).isEmpty()) << header.first.constData();
}

TEST_P(ProviderHeaders, SetAuthSchemeMovesTheKeyToAnotherHeader)
{
    auto client = makeClient();
    client->setAuthScheme(AuthScheme{
        AuthScheme::Placement::Header, QStringLiteral("X-Auth"), QStringLiteral("Token ")});
    const SentRequest sent = ask(client.get());

    EXPECT_EQ(sent.header("X-Auth"), QByteArray("Token sk-test"));

    if (authIsQueryParam(GetParam()))
        EXPECT_FALSE(QUrlQuery(sent.url()).hasQueryItem(GetParam().authQueryItem));
    else
        EXPECT_TRUE(sent.header(GetParam().authHeader).isEmpty());
}

TEST_P(ProviderHeaders, SetAuthSchemeMovesTheKeyToAQueryParam)
{
    auto client = makeClient();
    client->setAuthScheme(
        AuthScheme{AuthScheme::Placement::QueryParam, QStringLiteral("access_token"), QString()});
    const SentRequest sent = ask(client.get());

    EXPECT_EQ(
        QUrlQuery(sent.url()).queryItemValue(QStringLiteral("access_token")),
        QStringLiteral("sk-test"));

    if (!authIsQueryParam(GetParam()))
        EXPECT_TRUE(sent.header(GetParam().authHeader).isEmpty());
}

TEST_P(ProviderHeaders, NoneSchemeSendsNoCredential)
{
    auto client = makeClient();
    client->setAuthScheme(AuthScheme{AuthScheme::Placement::None, QStringLiteral("ignored"), {}});
    const SentRequest sent = ask(client.get());

    if (authIsQueryParam(GetParam()))
        EXPECT_FALSE(QUrlQuery(sent.url()).hasQueryItem(GetParam().authQueryItem));
    else
        EXPECT_TRUE(sent.header(GetParam().authHeader).isEmpty());
}

INSTANTIATE_TEST_SUITE_P(
    AllProviders,
    ProviderHeaders,
    ::testing::ValuesIn(providerCases()),
    [](const ::testing::TestParamInfo<ProviderCase> &info) { return std::string(info.param.name); });

TEST(ClaudePromptCaching, BetaHeaderIsSetByTheCallerNotTheClient)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);

    client.ask(QStringLiteral("hi"));
    ASSERT_EQ(transport.streamCount(), 1);
    EXPECT_TRUE(transport.streamRequest(0).header("anthropic-beta").isEmpty());

    ClaudeClient opted("http://fake.local", "sk-test", "claude-test", &transport);
    opted.setHeader(
        QStringLiteral("anthropic-beta"), QStringLiteral("extended-cache-ttl-2025-04-11"));
    opted.ask(QStringLiteral("hi"));

    ASSERT_EQ(transport.streamCount(), 2);
    const SentRequest sent = transport.streamRequest(1);
    EXPECT_EQ(sent.header("anthropic-beta"), QByteArray("extended-cache-ttl-2025-04-11"));
    EXPECT_EQ(sent.header("anthropic-version"), QByteArray("2023-06-01"));
    EXPECT_EQ(sent.header("x-api-key"), QByteArray("sk-test"));
}
