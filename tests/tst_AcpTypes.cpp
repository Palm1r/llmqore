// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include <LLMQore/AcpTypes.hpp>

#include "acp/AcpTypeSchema.hpp"

#include "JsonRoundTrip.hpp"

using namespace LLMQore::Acp;

namespace {

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
    s.args = QStringList{"-y", "@modelcontextprotocol/server-filesystem", "/home/user"};
    s.env.append(EnvVariable{"TOKEN", "abc"});
    p.mcpServers.append(s);
    p.additionalDirectories = QStringList{"/tmp"};

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

TEST(AcpTypes, SessionConfigOptionSelectRoundTrip)
{
    SessionConfigOption o;
    o.id = "model";
    o.name = "Model";
    o.description = "AI model to use";
    o.category = "model";
    o.type = "select";
    o.currentValue = "default";
    o.options.append(SessionConfigSelectOption{"default", "Default", "Opus 4.5"});
    o.options.append(SessionConfigSelectOption{"sonnet", "Sonnet", ""});

    const QJsonObject expected{
        {"id", "model"},
        {"name", "Model"},
        {"type", "select"},
        {"description", "AI model to use"},
        {"category", "model"},
        {"currentValue", "default"},
        {"options",
         QJsonArray{
             QJsonObject{{"value", "default"}, {"name", "Default"}, {"description", "Opus 4.5"}},
             QJsonObject{{"value", "sonnet"}, {"name", "Sonnet"}}}}};
    EXPECT_EQ(o.toJson(), expected);

    const SessionConfigOption back = SessionConfigOption::fromJson(expected);
    EXPECT_EQ(back.toJson(), expected);
    EXPECT_EQ(back.id, "model");
    EXPECT_EQ(back.category, "model");
    EXPECT_EQ(back.currentValue.toString(), "default");
    ASSERT_EQ(back.options.size(), 2);
    EXPECT_EQ(back.options.first().description, "Opus 4.5");
    EXPECT_TRUE(back.groups.isEmpty());
}

TEST(AcpTypes, SessionConfigSelectGroupedRoundTrip)
{
    SessionConfigOption o;
    o.id = "model";
    o.name = "Model";
    o.type = "select";
    o.currentValue = "opus";
    SessionConfigSelectGroup g;
    g.group = "anthropic";
    g.name = "Anthropic";
    g.options.append(SessionConfigSelectOption{"opus", "Opus", ""});
    g.options.append(SessionConfigSelectOption{"sonnet", "Sonnet", ""});
    o.groups.append(g);

    const QJsonObject expected{
        {"id", "model"},
        {"name", "Model"},
        {"type", "select"},
        {"currentValue", "opus"},
        {"options",
         QJsonArray{QJsonObject{
             {"group", "anthropic"},
             {"name", "Anthropic"},
             {"options",
              QJsonArray{
                  QJsonObject{{"value", "opus"}, {"name", "Opus"}},
                  QJsonObject{{"value", "sonnet"}, {"name", "Sonnet"}}}}}}}};
    EXPECT_EQ(o.toJson(), expected);

    const SessionConfigOption back = SessionConfigOption::fromJson(expected);
    EXPECT_EQ(back.toJson(), expected);
    EXPECT_TRUE(back.options.isEmpty());
    ASSERT_EQ(back.groups.size(), 1);
    EXPECT_EQ(back.groups.first().group, "anthropic");
    ASSERT_EQ(back.groups.first().options.size(), 2);
}

TEST(AcpTypes, SessionConfigOptionBooleanRoundTrip)
{
    SessionConfigOption o;
    o.id = "fast";
    o.name = "Fast mode";
    o.type = "boolean";
    o.currentValue = true;

    // The schema's boolean arm: currentValue is a JSON bool and there is no
    // options key at all.
    const QJsonObject expected{
        {"id", "fast"}, {"name", "Fast mode"}, {"type", "boolean"}, {"currentValue", true}};
    EXPECT_EQ(o.toJson(), expected);

    const SessionConfigOption back = SessionConfigOption::fromJson(expected);
    EXPECT_EQ(back.toJson(), expected);
    EXPECT_TRUE(back.currentValue.isBool());
    EXPECT_TRUE(back.currentValue.toBool());
}

TEST(AcpTypes, NewSessionResultCarriesConfigOptions)
{
    NewSessionResult r;
    r.sessionId = "sess-1";
    EXPECT_FALSE(r.toJson().contains("configOptions"));

    SessionConfigOption o;
    o.id = "effort";
    o.name = "Effort";
    o.type = "select";
    o.currentValue = "high";
    o.options.append(SessionConfigSelectOption{"default", "Default", ""});
    o.options.append(SessionConfigSelectOption{"high", "High", ""});
    r.configOptions.append(o);

    // NewSessionResponse in the schema: sessionId plus the configOptions array.
    const QJsonObject expected{
        {"sessionId", "sess-1"},
        {"configOptions",
         QJsonArray{QJsonObject{
             {"id", "effort"},
             {"name", "Effort"},
             {"type", "select"},
             {"currentValue", "high"},
             {"options",
              QJsonArray{
                  QJsonObject{{"value", "default"}, {"name", "Default"}},
                  QJsonObject{{"value", "high"}, {"name", "High"}}}}}}}};
    EXPECT_EQ(r.toJson(), expected);

    const NewSessionResult back = NewSessionResult::fromJson(expected);
    EXPECT_EQ(back.toJson(), expected);
    ASSERT_EQ(back.configOptions.size(), 1);
    EXPECT_EQ(back.configOptions.first().id, "effort");
}

TEST(AcpTypes, SessionUpdateConfigOptionsRoundTrip)
{
    SessionUpdate u;
    u.sessionUpdate = SessionUpdateKind::ConfigOptionUpdate;
    SessionConfigOption o;
    o.id = "model";
    o.name = "Model";
    o.type = "select";
    o.currentValue = "sonnet";
    o.options.append(SessionConfigSelectOption{"sonnet", "Sonnet", ""});
    u.configOptions.append(o);

    // ConfigOptionUpdate in the schema: the sessionUpdate tag plus configOptions.
    const QJsonObject expected{
        {"sessionUpdate", "config_option_update"},
        {"configOptions",
         QJsonArray{QJsonObject{
             {"id", "model"},
             {"name", "Model"},
             {"type", "select"},
             {"currentValue", "sonnet"},
             {"options", QJsonArray{QJsonObject{{"value", "sonnet"}, {"name", "Sonnet"}}}}}}}};
    EXPECT_EQ(u.toJson(), expected);

    const SessionUpdate back = SessionUpdate::fromJson(expected);
    EXPECT_EQ(back.toJson(), expected);
    ASSERT_EQ(back.configOptions.size(), 1);
    EXPECT_EQ(back.configOptions.first().id, "model");
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

TEST(AcpTypes, SessionUpdateUsageRoundTrip)
{
    SessionUpdate u;
    u.sessionUpdate = SessionUpdateKind::UsageUpdate;
    u.usage = QJsonObject{{"inputTokens", 120}, {"outputTokens", 34}};

    const QJsonObject obj = u.toJson();
    EXPECT_EQ(obj.value("sessionUpdate").toString(), "usage_update");
    EXPECT_EQ(obj.value("inputTokens").toInt(), 120);
    EXPECT_EQ(obj.value("outputTokens").toInt(), 34);

    const SessionUpdate back = SessionUpdate::fromJson(obj);
    EXPECT_EQ(back.sessionUpdate, "usage_update");
    EXPECT_EQ(back.usage, u.usage)
        << "usage must come back exactly as written, without the envelope's own key";
}

TEST(AcpTypes, SessionUpdateSessionInfoRoundTrip)
{
    SessionUpdate u;
    u.sessionUpdate = SessionUpdateKind::SessionInfoUpdate;
    u.title = "Refactor the parser";

    const QJsonObject obj = u.toJson();
    EXPECT_EQ(obj.value("sessionUpdate").toString(), "session_info_update");
    EXPECT_EQ(obj.value("title").toString(), "Refactor the parser");

    const SessionUpdate back = SessionUpdate::fromJson(obj);
    EXPECT_EQ(back.title, "Refactor the parser");
}

TEST(AcpTypes, SessionUpdateUnknownKindKeepsItsPayloadOut)
{
    SessionUpdate u;
    u.sessionUpdate = SessionUpdateKind::CurrentModeUpdate;
    u.currentModeId = "plan";
    u.usage = QJsonObject{{"inputTokens", 5}};
    u.title = "ignored";

    const QJsonObject obj = u.toJson();
    EXPECT_EQ(obj.value("currentModeId").toString(), "plan");
    EXPECT_FALSE(obj.contains("inputTokens"));
    EXPECT_FALSE(obj.contains("title"));
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
    c.args = QStringList{"-la"};
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


namespace LLMQoreTest {

template<>
QJsonObject sampleObject<ContentBlock>(int)
{
    return ContentBlock::makeText(QStringLiteral("hello")).toJson();
}

template<>
QJsonObject sampleObject<ToolCallContent>(int)
{
    ToolCallContent c;
    c.content = ContentBlock::makeText(QStringLiteral("hello"));
    return c.toJson();
}

template<>
QJsonObject sampleObject<McpServer>(int)
{
    return McpServer::stdio(
               QStringLiteral("fs"), QStringLiteral("npx"), {QStringLiteral("-y")})
        .toJson();
}

template<>
QJsonObject sampleObject<SessionUpdate>(int)
{
    SessionUpdate u;
    u.sessionUpdate = QLatin1String(SessionUpdateKind::AgentMessageChunk);
    u.content = ContentBlock::makeText(QStringLiteral("hi"));
    return u.toJson();
}

} // namespace LLMQoreTest

namespace {

TEST(AcpTypes, EveryTabledStructureHandsBackWhatItWasGiven)
{
    using LLMQoreTest::expectRoundTrip;

    expectRoundTrip<EnvVariable>("EnvVariable");
    expectRoundTrip<Implementation>("Implementation");
    expectRoundTrip<FileSystemCapability>("FileSystemCapability");
    expectRoundTrip<ClientCapabilities>("ClientCapabilities");
    expectRoundTrip<PromptCapabilities>("PromptCapabilities");
    expectRoundTrip<McpCapabilities>("McpCapabilities");
    expectRoundTrip<AgentCapabilities>("AgentCapabilities");
    expectRoundTrip<AuthMethod>("AuthMethod");
    expectRoundTrip<InitializeParams>("InitializeParams");
    expectRoundTrip<InitializeResult>("InitializeResult");
    expectRoundTrip<SessionMode>("SessionMode");
    expectRoundTrip<SessionModeState>("SessionModeState");
    expectRoundTrip<SessionConfigSelectOption>("SessionConfigSelectOption");
    expectRoundTrip<SessionConfigSelectGroup>("SessionConfigSelectGroup");
    expectRoundTrip<NewSessionParams>("NewSessionParams");
    expectRoundTrip<NewSessionResult>("NewSessionResult");
    expectRoundTrip<LoadSessionParams>("LoadSessionParams");
    expectRoundTrip<PromptParams>("PromptParams");
    expectRoundTrip<PromptResult>("PromptResult");
    expectRoundTrip<ToolCallLocation>("ToolCallLocation");
    expectRoundTrip<ToolCall>("ToolCall");
    expectRoundTrip<PlanEntry>("PlanEntry");
    expectRoundTrip<Plan>("Plan");
    expectRoundTrip<SessionNotification>("SessionNotification");
    expectRoundTrip<PermissionOption>("PermissionOption");
    expectRoundTrip<RequestPermissionParams>("RequestPermissionParams");
    expectRoundTrip<ReadTextFileParams>("ReadTextFileParams");
    expectRoundTrip<ReadTextFileResult>("ReadTextFileResult");
    expectRoundTrip<WriteTextFileParams>("WriteTextFileParams");
    expectRoundTrip<CreateTerminalParams>("CreateTerminalParams");
    expectRoundTrip<CreateTerminalResult>("CreateTerminalResult");
    expectRoundTrip<ExitStatus>("ExitStatus");
    expectRoundTrip<TerminalOutputParams>("TerminalOutputParams");
    expectRoundTrip<TerminalOutputResult>("TerminalOutputResult");
    expectRoundTrip<TerminalRefParams>("TerminalRefParams");
    expectRoundTrip<WaitForTerminalExitResult>("WaitForTerminalExitResult");
}

TEST(AcpTypes, PromptResultKeepsItsUsage)
{
    const QJsonObject wire{
        {"stopReason", QLatin1String(StopReason::EndTurn)},
        {"usage", QJsonObject{{"inputTokens", 12}, {"outputTokens", 34}}},
    };

    const PromptResult r = PromptResult::fromJson(wire);
    EXPECT_EQ(r.usage.value("inputTokens").toInt(), 12);
    EXPECT_EQ(r.toJson().value("usage").toObject(), wire.value("usage").toObject())
        << "a loopback agent reporting usage would have lost it silently";
}

TEST(AcpTypes, HandshakeStructuresCarryUnknownFieldsThrough)
{
    using LLMQoreTest::expectUnknownKeySurvives;

    expectUnknownKeySurvives<ClientCapabilities>("ClientCapabilities");
    expectUnknownKeySurvives<AgentCapabilities>("AgentCapabilities");
    expectUnknownKeySurvives<InitializeParams>("InitializeParams");
    expectUnknownKeySurvives<InitializeResult>("InitializeResult");
    expectUnknownKeySurvives<NewSessionResult>("NewSessionResult");
    expectUnknownKeySurvives<PromptResult>("PromptResult");
}

} // namespace
