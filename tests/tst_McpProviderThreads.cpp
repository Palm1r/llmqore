// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QThread>

#include <LLMQore/BasePromptProvider.hpp>
#include <LLMQore/CallbackElicitationProvider.hpp>
#include <LLMQore/DirectoryRootsProvider.hpp>
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/McpClient.hpp>
#include <LLMQore/McpServer.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/RpcPipeTransport.hpp>
#include <LLMQore/StaticResourceProvider.hpp>

using namespace LLMQore;
using namespace LLMQore::Mcp;

#ifndef QT_NO_DEBUG

namespace {

class NoPrompts : public BasePromptProvider
{
public:
    QFuture<QList<PromptInfo>> listPrompts() override { return readyFuture(QList<PromptInfo>{}); }
    QFuture<PromptGetResult> getPrompt(const QString &, const QJsonObject &) override
    {
        return readyFuture(PromptGetResult{});
    }
};

class ProviderThreadTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        GTEST_FLAG_SET(death_test_style, "threadsafe");
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_McpProviderThreads";
            static char *argv[] = {arg0};
            m_app = new QCoreApplication(argc, argv);
        }
        auto [server, client] = Rpc::PipeTransport::createPair();
        m_serverTransport.reset(server);
        m_clientTransport.reset(client);
    }

    void TearDown() override
    {
        m_serverTransport.reset();
        m_clientTransport.reset();
        delete m_app;
        m_app = nullptr;
    }

    template<typename T>
    T *elsewhere(T *object)
    {
        object->moveToThread(&m_elsewhere);
        m_strays.emplace_back(object);
        return object;
    }

    Rpc::Transport *serverTransport() const { return m_serverTransport.get(); }
    Rpc::Transport *clientTransport() const { return m_clientTransport.get(); }

private:
    QCoreApplication *m_app = nullptr;
    std::unique_ptr<Rpc::Transport> m_serverTransport;
    std::unique_ptr<Rpc::Transport> m_clientTransport;
    QThread m_elsewhere;
    std::vector<std::unique_ptr<QObject>> m_strays;
};

constexpr const char *kWrongThread = "same thread";

} // namespace

TEST_F(ProviderThreadTest, ClientRefusesASamplingClientFromAnotherThread)
{
    McpClient client(clientTransport());
    auto *sampler = elsewhere(new OllamaClient);
    EXPECT_DEATH(client.setSamplingClient(sampler, {}), kWrongThread);
}

TEST_F(ProviderThreadTest, ClientRefusesAnElicitationProviderFromAnotherThread)
{
    McpClient client(clientTransport());
    auto *provider = elsewhere(new CallbackElicitationProvider(nullptr));
    EXPECT_DEATH(client.setElicitationProvider(provider), kWrongThread);
}

TEST_F(ProviderThreadTest, ClientRefusesARootsProviderFromAnotherThread)
{
    McpClient client(clientTransport());
    auto *provider = elsewhere(new DirectoryRootsProvider);
    EXPECT_DEATH(client.setRootsProvider(provider), kWrongThread);
}

TEST_F(ProviderThreadTest, ServerRefusesAResourceProviderFromAnotherThread)
{
    McpServer server(serverTransport(), McpServerConfig{{"threads", "0.0.1"}});
    auto *provider = elsewhere(new StaticResourceProvider);
    EXPECT_DEATH(server.addResourceProvider(provider), kWrongThread);
}

TEST_F(ProviderThreadTest, ServerRefusesAPromptProviderFromAnotherThread)
{
    McpServer server(serverTransport(), McpServerConfig{{"threads", "0.0.1"}});
    auto *provider = elsewhere(new NoPrompts);
    EXPECT_DEATH(server.addPromptProvider(provider), kWrongThread);
}

TEST_F(ProviderThreadTest, ProvidersFromTheSameThreadAreAccepted)
{
    McpClient client(clientTransport());
    McpServer server(serverTransport(), McpServerConfig{{"threads", "0.0.1"}});
    OllamaClient sampler;
    CallbackElicitationProvider elicitation(nullptr);
    DirectoryRootsProvider roots;
    StaticResourceProvider resources;
    NoPrompts prompts;

    client.setSamplingClient(&sampler, {});
    client.setElicitationProvider(&elicitation);
    client.setRootsProvider(&roots);
    server.addResourceProvider(&resources);
    server.addPromptProvider(&prompts);

    client.setSamplingClient(nullptr, {});
    client.setElicitationProvider(nullptr);
    client.setRootsProvider(nullptr);
    server.removeResourceProvider(&resources);
    server.removePromptProvider(&prompts);
    SUCCEED();
}

#endif
