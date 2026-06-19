// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include <LLMQore/AcpTypes.hpp>

using namespace LLMQore::Acp;

namespace {

// A representative initialize request round-trips, including nested
// capabilities and optional clientInfo.
TEST(AcpTypes, InitializeParamsRoundTrip)
{
    InitializeParams p;
    p.protocolVersion = 1;
    p.clientCapabilities.fs.readTextFile = true;
    p.clientCapabilities.fs.writeTextFile = true;
    p.clientCapabilities.terminal = true;
    p.clientInfo = Implementation{"llmqore-host", "0.6.0", "LLMQore"};

    const InitializeParams back = InitializeParams::fromJson(p.toJson());
    EXPECT_EQ(back.protocolVersion, 1);
    EXPECT_TRUE(back.clientCapabilities.fs.readTextFile);
    EXPECT_TRUE(back.clientCapabilities.fs.writeTextFile);
    EXPECT_TRUE(back.clientCapabilities.terminal);
    ASSERT_TRUE(back.clientInfo.has_value());
    EXPECT_EQ(back.clientInfo->name, "llmqore-host");
    EXPECT_EQ(back.clientInfo->title, "LLMQore");
}

// protocolVersion must serialize as a JSON number, not a string (ACP gotcha).
TEST(AcpTypes, ProtocolVersionIsJsonNumber)
{
    InitializeParams p;
    p.protocolVersion = 1;
    const QJsonObject obj = p.toJson();
    EXPECT_TRUE(obj.value("protocolVersion").isDouble());
    EXPECT_FALSE(obj.value("protocolVersion").isString());
    EXPECT_EQ(obj.value("protocolVersion").toInt(), 1);
}

TEST(AcpTypes, InitializeResultRoundTrip)
{
    InitializeResult r;
    r.protocolVersion = 1;
    r.agentCapabilities.loadSession = true;
    r.agentCapabilities.promptCapabilities.image = true;
    r.agentCapabilities.promptCapabilities.embeddedContext = true;
    r.authMethods.append(AuthMethod{"oauth", "OAuth", "Sign in"});

    const InitializeResult back = InitializeResult::fromJson(r.toJson());
    EXPECT_EQ(back.protocolVersion, 1);
    EXPECT_TRUE(back.agentCapabilities.loadSession);
    EXPECT_TRUE(back.agentCapabilities.promptCapabilities.image);
    EXPECT_FALSE(back.agentCapabilities.promptCapabilities.audio);
    ASSERT_EQ(back.authMethods.size(), 1);
    EXPECT_EQ(back.authMethods.first().id, "oauth");
}

TEST(AcpTypes, NewSessionParamsCarriesMcpServers)
{
    NewSessionParams p;
    p.cwd = "/home/user/project";
    McpServer s;
    s.name = "filesystem";
    s.command = "npx";
    s.args = {"-y", "@modelcontextprotocol/server-filesystem", "/home/user"};
    s.env.append(EnvVariable{"TOKEN", "abc"});
    p.mcpServers.append(s);
    p.additionalDirectories = {"/tmp"};

    const NewSessionParams back = NewSessionParams::fromJson(p.toJson());
    EXPECT_EQ(back.cwd, "/home/user/project");
    ASSERT_EQ(back.mcpServers.size(), 1);
    EXPECT_EQ(back.mcpServers.first().command, "npx");
    ASSERT_EQ(back.mcpServers.first().args.size(), 3);
    ASSERT_EQ(back.mcpServers.first().env.size(), 1);
    EXPECT_EQ(back.mcpServers.first().env.first().name, "TOKEN");
    ASSERT_EQ(back.additionalDirectories.size(), 1);
}

TEST(AcpTypes, NewSessionResultModesOptional)
{
    NewSessionResult r;
    r.sessionId = "sess-1";
    EXPECT_FALSE(NewSessionResult::fromJson(r.toJson()).modes.has_value());

    SessionModeState modes;
    modes.currentModeId = "ask";
    modes.availableModes.append(SessionMode{"ask", "Ask", ""});
    modes.availableModes.append(SessionMode{"code", "Code", "Edit freely"});
    r.modes = modes;

    const NewSessionResult back = NewSessionResult::fromJson(r.toJson());
    EXPECT_EQ(back.sessionId, "sess-1");
    ASSERT_TRUE(back.modes.has_value());
    EXPECT_EQ(back.modes->currentModeId, "ask");
    ASSERT_EQ(back.modes->availableModes.size(), 2);
}

TEST(AcpTypes, ContentBlockTextRoundTrip)
{
    const ContentBlock b = ContentBlock::makeText("hello");
    const QJsonObject obj = b.toJson();
    EXPECT_EQ(obj.value("type").toString(), "text");
    EXPECT_EQ(obj.value("text").toString(), "hello");
    EXPECT_EQ(ContentBlock::fromJson(obj).text, "hello");
}

TEST(AcpTypes, ContentBlockImageAndResource)
{
    ContentBlock img;
    img.type = "image";
    img.data = "QUJD";
    img.mimeType = "image/png";
    const ContentBlock imgBack = ContentBlock::fromJson(img.toJson());
    EXPECT_EQ(imgBack.data, "QUJD");
    EXPECT_EQ(imgBack.mimeType, "image/png");

    ContentBlock res;
    res.type = "resource";
    EmbeddedResource er;
    er.uri = "file:///a.txt";
    er.mimeType = "text/plain";
    er.text = "body";
    res.resource = er;
    const ContentBlock resBack = ContentBlock::fromJson(res.toJson());
    ASSERT_TRUE(resBack.resource.has_value());
    EXPECT_EQ(resBack.resource->uri, "file:///a.txt");
    EXPECT_EQ(resBack.resource->text, "body");
}

TEST(AcpTypes, PromptParamsRoundTrip)
{
    PromptParams p;
    p.sessionId = "sess-9";
    p.prompt = {ContentBlock::makeText("do the thing")};
    const PromptParams back = PromptParams::fromJson(p.toJson());
    EXPECT_EQ(back.sessionId, "sess-9");
    ASSERT_EQ(back.prompt.size(), 1);
    EXPECT_EQ(back.prompt.first().text, "do the thing");
}

TEST(AcpTypes, ToolCallRoundTripWithContentAndLocations)
{
    ToolCall t;
    t.toolCallId = "call-1";
    t.title = "Edit main.cpp";
    t.kind = "edit";
    t.status = "in_progress";
    ToolCallContent c;
    c.type = "content";
    c.content = ContentBlock::makeText("patch");
    t.content.append(c);
    ToolCallLocation loc;
    loc.path = "/src/main.cpp";
    loc.line = 42;
    t.locations.append(loc);

    const ToolCall back = ToolCall::fromJson(t.toJson());
    EXPECT_EQ(back.toolCallId, "call-1");
    EXPECT_EQ(back.kind, "edit");
    EXPECT_EQ(back.status, "in_progress");
    ASSERT_EQ(back.content.size(), 1);
    ASSERT_TRUE(back.content.first().content.has_value());
    EXPECT_EQ(back.content.first().content->text, "patch");
    ASSERT_EQ(back.locations.size(), 1);
    ASSERT_TRUE(back.locations.first().line.has_value());
    EXPECT_EQ(*back.locations.first().line, 42);
}

TEST(AcpTypes, SessionUpdateAgentMessageChunk)
{
    SessionUpdate u;
    u.sessionUpdate = SessionUpdateKind::AgentMessageChunk;
    u.content = ContentBlock::makeText("partial answer");

    const QJsonObject obj = u.toJson();
    EXPECT_EQ(obj.value("sessionUpdate").toString(), "agent_message_chunk");

    const SessionUpdate back = SessionUpdate::fromJson(obj);
    ASSERT_TRUE(back.content.has_value());
    EXPECT_EQ(back.content->text, "partial answer");
}

TEST(AcpTypes, SessionUpdateToolCallAndPlan)
{
    SessionUpdate tc;
    tc.sessionUpdate = SessionUpdateKind::ToolCall;
    ToolCall t;
    t.toolCallId = "x";
    t.status = "pending";
    tc.toolCall = t;
    const SessionUpdate tcBack = SessionUpdate::fromJson(tc.toJson());
    ASSERT_TRUE(tcBack.toolCall.has_value());
    EXPECT_EQ(tcBack.toolCall->toolCallId, "x");

    SessionUpdate pl;
    pl.sessionUpdate = SessionUpdateKind::Plan;
    Plan plan;
    plan.entries.append(PlanEntry{"step 1", "high", "pending"});
    pl.plan = plan;
    const SessionUpdate plBack = SessionUpdate::fromJson(pl.toJson());
    ASSERT_TRUE(plBack.plan.has_value());
    ASSERT_EQ(plBack.plan->entries.size(), 1);
    EXPECT_EQ(plBack.plan->entries.first().priority, "high");
}

TEST(AcpTypes, SessionNotificationRoundTrip)
{
    SessionNotification n;
    n.sessionId = "sess-2";
    n.update.sessionUpdate = SessionUpdateKind::AgentThoughtChunk;
    n.update.content = ContentBlock::makeText("thinking...");

    const SessionNotification back = SessionNotification::fromJson(n.toJson());
    EXPECT_EQ(back.sessionId, "sess-2");
    EXPECT_EQ(back.update.sessionUpdate, "agent_thought_chunk");
    ASSERT_TRUE(back.update.content.has_value());
    EXPECT_EQ(back.update.content->text, "thinking...");
}

TEST(AcpTypes, RequestPermissionRoundTrip)
{
    RequestPermissionParams p;
    p.sessionId = "sess-3";
    p.toolCall.toolCallId = "c1";
    p.toolCall.title = "rm -rf";
    p.options.append(PermissionOption{"a", "Allow", PermissionOptionKind::AllowOnce});
    p.options.append(PermissionOption{"r", "Reject", PermissionOptionKind::RejectOnce});

    const RequestPermissionParams back = RequestPermissionParams::fromJson(p.toJson());
    EXPECT_EQ(back.sessionId, "sess-3");
    EXPECT_EQ(back.toolCall.toolCallId, "c1");
    ASSERT_EQ(back.options.size(), 2);
    EXPECT_EQ(back.options.first().kind, "allow_once");
}

TEST(AcpTypes, RequestPermissionResultSelectedAndCancelled)
{
    const RequestPermissionResult sel = RequestPermissionResult::selected("a");
    const QJsonObject selObj = sel.toJson();
    EXPECT_EQ(selObj.value("outcome").toObject().value("outcome").toString(), "selected");
    EXPECT_EQ(selObj.value("outcome").toObject().value("optionId").toString(), "a");
    const RequestPermissionResult selBack = RequestPermissionResult::fromJson(selObj);
    EXPECT_EQ(selBack.outcome, "selected");
    EXPECT_EQ(selBack.optionId, "a");

    const RequestPermissionResult can = RequestPermissionResult::cancelled();
    const RequestPermissionResult canBack = RequestPermissionResult::fromJson(can.toJson());
    EXPECT_EQ(canBack.outcome, "cancelled");
}

TEST(AcpTypes, ReadWriteFileParams)
{
    ReadTextFileParams r;
    r.sessionId = "s";
    r.path = "/a.txt";
    r.line = 10;
    r.limit = 50;
    const ReadTextFileParams rBack = ReadTextFileParams::fromJson(r.toJson());
    ASSERT_TRUE(rBack.line.has_value());
    EXPECT_EQ(*rBack.line, 10);
    ASSERT_TRUE(rBack.limit.has_value());
    EXPECT_EQ(*rBack.limit, 50);

    // line/limit omitted when unset.
    ReadTextFileParams r2;
    r2.sessionId = "s";
    r2.path = "/b.txt";
    const QJsonObject r2obj = r2.toJson();
    EXPECT_FALSE(r2obj.contains("line"));
    EXPECT_FALSE(r2obj.contains("limit"));

    WriteTextFileParams w;
    w.sessionId = "s";
    w.path = "/c.txt";
    w.content = "data";
    const WriteTextFileParams wBack = WriteTextFileParams::fromJson(w.toJson());
    EXPECT_EQ(wBack.content, "data");
}

TEST(AcpTypes, TerminalParamsAndExitStatus)
{
    CreateTerminalParams c;
    c.sessionId = "s";
    c.command = "ls";
    c.args = {"-la"};
    c.cwd = "/tmp";
    c.env.append(EnvVariable{"FOO", "bar"});
    c.outputByteLimit = 1024;
    const CreateTerminalParams cBack = CreateTerminalParams::fromJson(c.toJson());
    EXPECT_EQ(cBack.command, "ls");
    ASSERT_EQ(cBack.args.size(), 1);
    ASSERT_EQ(cBack.env.size(), 1);
    ASSERT_TRUE(cBack.outputByteLimit.has_value());
    EXPECT_EQ(*cBack.outputByteLimit, 1024);

    TerminalOutputResult o;
    o.output = "hello";
    o.truncated = true;
    ExitStatus es;
    es.exitCode = 0;
    o.exitStatus = es;
    const TerminalOutputResult oBack = TerminalOutputResult::fromJson(o.toJson());
    EXPECT_EQ(oBack.output, "hello");
    EXPECT_TRUE(oBack.truncated);
    ASSERT_TRUE(oBack.exitStatus.has_value());
    ASSERT_TRUE(oBack.exitStatus->exitCode.has_value());
    EXPECT_EQ(*oBack.exitStatus->exitCode, 0);
}

} // namespace
