// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonObject>

#include <LLMQore/AcpAgentConfig.hpp>

using namespace LLMQore::Acp;

namespace {

TEST(AcpAgentConfig, ToLaunchConfigMapsFieldsAndEnv)
{
    AcpAgentConfig c;
    c.command = "myagent";
    c.args = QStringList{"--acp"};
    c.cwd = "/proj";
    c.env.append(EnvVariable{"API_KEY", "secret"});

    const auto launch = c.toLaunchConfig();
    EXPECT_EQ(launch.program, "myagent");
    ASSERT_EQ(launch.arguments.size(), 1);
    EXPECT_EQ(launch.arguments.first(), "--acp");
    EXPECT_EQ(launch.workingDirectory, "/proj");
    EXPECT_EQ(launch.environment.value("API_KEY"), "secret");
}

TEST(AcpAgentConfig, JsonRoundTrip)
{
    AcpAgentConfig c;
    c.command = "npx";
    c.args = QStringList{"-y", "@scope/agent-acp"};
    c.cwd = "/home/user";
    c.env.append(EnvVariable{"TOKEN", "abc"});

    const AcpAgentConfig back = AcpAgentConfig::fromJson(c.toJson());
    EXPECT_EQ(back.command, "npx");
    ASSERT_EQ(back.args.size(), 2);
    EXPECT_EQ(back.args.last(), "@scope/agent-acp");
    EXPECT_EQ(back.cwd, "/home/user");
    ASSERT_EQ(back.env.size(), 1);
    EXPECT_EQ(back.env.first().name, "TOKEN");
    EXPECT_EQ(back.env.first().value, "abc");
}

TEST(AcpAgentConfig, CreateTransportReturnsUnstartedTransport)
{
    AcpAgentConfig c;
    c.command = "gemini";
    c.args = QStringList{"--experimental-acp"};
    auto *transport = c.createTransport();
    ASSERT_NE(transport, nullptr);
    EXPECT_FALSE(transport->isOpen()); // not started yet
    delete transport;
}

} // namespace
