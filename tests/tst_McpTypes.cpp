// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>

#include <LLMCore/McpTypes.hpp>

using namespace LLMCore::Mcp;

TEST(McpTypesTest, ImplementationRoundTrip)
{
    Implementation impl{"MyServer", "1.2.3"};
    const QJsonObject obj = impl.toJson();
    EXPECT_EQ(obj.value("name").toString(), "MyServer");
    EXPECT_EQ(obj.value("version").toString(), "1.2.3");

    const Implementation back = Implementation::fromJson(obj);
    EXPECT_EQ(back.name, "MyServer");
    EXPECT_EQ(back.version, "1.2.3");
}

TEST(McpTypesTest, ToolInfoRoundTrip)
{
    ToolInfo info;
    info.name = "calculator";
    info.description = "Does math";
    info.inputSchema = QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{{"a", QJsonObject{{"type", "number"}}}}},
    };

    const QJsonObject obj = info.toJson();
    EXPECT_EQ(obj.value("name").toString(), "calculator");
    EXPECT_EQ(obj.value("description").toString(), "Does math");
    EXPECT_TRUE(obj.value("inputSchema").isObject());

    const ToolInfo back = ToolInfo::fromJson(obj);
    EXPECT_EQ(back.name, info.name);
    EXPECT_EQ(back.description, info.description);
    EXPECT_EQ(back.inputSchema, info.inputSchema);
}

// Note: ToolResult coverage moved to tst_ToolResult.cpp — the tool-call
// envelope is now represented by LLMCore::ToolResult, not a MCP-specific
// struct.

TEST(McpTypesTest, InitializeResultRoundTrip)
{
    InitializeResult init;
    init.protocolVersion = kSupportedProtocolVersion;
    ToolsCapability tc;
    tc.listChanged = true;
    init.capabilities.tools = tc;
    init.serverInfo = Implementation{"calc-mcp", "0.1.0"};

    const QJsonObject obj = init.toJson();
    const InitializeResult back = InitializeResult::fromJson(obj);

    EXPECT_EQ(back.protocolVersion, init.protocolVersion);
    ASSERT_TRUE(back.capabilities.tools.has_value());
    EXPECT_TRUE(back.capabilities.tools->listChanged);
    EXPECT_EQ(back.serverInfo.name, "calc-mcp");
    EXPECT_EQ(back.serverInfo.version, "0.1.0");
}

TEST(McpTypesTest, ResourceContentsRoundTripText)
{
    ResourceContents contents;
    contents.uri = "file:///tmp/hello.txt";
    contents.mimeType = "text/plain";
    contents.text = "hello world";

    const QJsonObject obj = contents.toJson();
    const ResourceContents back = ResourceContents::fromJson(obj);
    EXPECT_EQ(back.uri, contents.uri);
    EXPECT_EQ(back.mimeType, contents.mimeType);
    EXPECT_EQ(back.text, contents.text);
}

TEST(McpTypesTest, ToolInfoRoundTripWithTitleIconsAndMeta)
{
    ToolInfo info;
    info.name = "calc";
    info.title = "Calculator";
    info.inputSchema = QJsonObject{{"type", "object"}};
    IconInfo icon;
    icon.src = "data:image/png;base64,AAAA";
    icon.mimeType = "image/png";
    icon.sizes = "16x16";
    info.icons = {icon};
    info.meta = QJsonObject{{"vendor", "acme"}};

    const QJsonObject obj = info.toJson();
    EXPECT_EQ(obj.value("title").toString(), "Calculator");
    EXPECT_TRUE(obj.contains("icons"));
    EXPECT_EQ(obj.value("_meta").toObject().value("vendor").toString(), "acme");

    const ToolInfo back = ToolInfo::fromJson(obj);
    EXPECT_EQ(back.title, "Calculator");
    ASSERT_EQ(back.icons.size(), 1);
    EXPECT_EQ(back.icons.first().sizes, "16x16");
    EXPECT_EQ(back.meta.value("vendor").toString(), "acme");
}

TEST(McpTypesTest, PromptInfoAndGetResultRoundTrip)
{
    PromptInfo info;
    info.name = "greet";
    info.description = "says hi";
    PromptArgument arg;
    arg.name = "who";
    arg.required = true;
    info.arguments = {arg};

    const PromptInfo back = PromptInfo::fromJson(info.toJson());
    EXPECT_EQ(back.name, "greet");
    ASSERT_EQ(back.arguments.size(), 1);
    EXPECT_EQ(back.arguments.first().name, "who");
    EXPECT_TRUE(back.arguments.first().required);

    PromptGetResult result;
    result.description = "Greeting";
    PromptMessage m;
    m.role = "user";
    m.content = QJsonObject{{"type", "text"}, {"text", "hello"}};
    result.messages = {m};

    const PromptGetResult backResult = PromptGetResult::fromJson(result.toJson());
    ASSERT_EQ(backResult.messages.size(), 1);
    EXPECT_EQ(backResult.messages.first().role, "user");
    EXPECT_EQ(backResult.messages.first().content.value("text").toString(), "hello");
}

TEST(McpTypesTest, ResourceTemplateAndRootRoundTrip)
{
    ResourceTemplate t;
    t.uriTemplate = "mem://log/{date}";
    t.name = "log";
    t.mimeType = "text/plain";
    const ResourceTemplate back = ResourceTemplate::fromJson(t.toJson());
    EXPECT_EQ(back.uriTemplate, "mem://log/{date}");
    EXPECT_EQ(back.name, "log");

    Root r;
    r.uri = "file:///workspace";
    r.name = "workspace";
    const Root backR = Root::fromJson(r.toJson());
    EXPECT_EQ(backR.uri, "file:///workspace");
    EXPECT_EQ(backR.name, "workspace");
}

TEST(McpTypesTest, ServerCapabilitiesPreservePromptsAndLogging)
{
    ServerCapabilities caps;
    caps.tools = ToolsCapability{true};
    caps.prompts = PromptsCapability{true};
    caps.logging = LoggingCapability{true};

    const QJsonObject obj = caps.toJson();
    EXPECT_TRUE(obj.contains("prompts"));
    EXPECT_TRUE(obj.contains("logging"));

    const ServerCapabilities back = ServerCapabilities::fromJson(obj);
    EXPECT_TRUE(back.prompts.has_value());
    EXPECT_TRUE(back.logging.has_value());
    ASSERT_TRUE(back.prompts.has_value());
    EXPECT_TRUE(back.prompts->listChanged);
}

TEST(McpTypesTest, ClientCapabilitiesRoots)
{
    ClientCapabilities caps;
    RootsCapability rc;
    rc.listChanged = true;
    caps.roots = rc;

    const QJsonObject obj = caps.toJson();
    EXPECT_TRUE(obj.contains("roots"));
    const ClientCapabilities back = ClientCapabilities::fromJson(obj);
    ASSERT_TRUE(back.roots.has_value());
    EXPECT_TRUE(back.roots->listChanged);
}

TEST(McpTypesTest, CompletionTypesRoundTrip)
{
    CompletionReference ref;
    ref.type = "ref/prompt";
    ref.name = "code_review";
    const QJsonObject refObj = ref.toJson();
    EXPECT_EQ(refObj.value("type").toString(), "ref/prompt");
    EXPECT_EQ(refObj.value("name").toString(), "code_review");
    EXPECT_FALSE(refObj.contains("uri"));
    const CompletionReference refBack = CompletionReference::fromJson(refObj);
    EXPECT_EQ(refBack.type, ref.type);
    EXPECT_EQ(refBack.name, ref.name);

    CompletionReference refRes;
    refRes.type = "ref/resource";
    refRes.uri = "file:///logs/{date}.log";
    const CompletionReference refResBack = CompletionReference::fromJson(refRes.toJson());
    EXPECT_EQ(refResBack.type, refRes.type);
    EXPECT_EQ(refResBack.uri, refRes.uri);
    EXPECT_TRUE(refResBack.name.isEmpty());

    CompletionArgument arg;
    arg.name = "language";
    arg.value = "py";
    const CompletionArgument argBack = CompletionArgument::fromJson(arg.toJson());
    EXPECT_EQ(argBack.name, arg.name);
    EXPECT_EQ(argBack.value, arg.value);

    CompletionResult res;
    res.values = {"python", "pyramid", "pytorch"};
    res.total = 10;
    res.hasMore = true;
    const QJsonObject resObj = res.toJson();
    ASSERT_TRUE(resObj.contains("completion"));
    const QJsonObject inner = resObj.value("completion").toObject();
    EXPECT_EQ(inner.value("values").toArray().size(), 3);
    EXPECT_EQ(inner.value("total").toInt(), 10);
    EXPECT_TRUE(inner.value("hasMore").toBool());

    const CompletionResult back = CompletionResult::fromJson(resObj);
    EXPECT_EQ(back.values, res.values);
    ASSERT_TRUE(back.total.has_value());
    EXPECT_EQ(*back.total, 10);
    EXPECT_TRUE(back.hasMore);

    // Empty result default encoding
    const CompletionResult empty;
    const CompletionResult emptyBack = CompletionResult::fromJson(empty.toJson());
    EXPECT_TRUE(emptyBack.values.isEmpty());
    EXPECT_FALSE(emptyBack.total.has_value());
    EXPECT_FALSE(emptyBack.hasMore);
}

TEST(McpTypesTest, ServerCapabilitiesCompletionsFlag)
{
    ServerCapabilities caps;
    caps.completions = CompletionsCapability{true};

    const QJsonObject obj = caps.toJson();
    EXPECT_TRUE(obj.contains("completions"));

    const ServerCapabilities back = ServerCapabilities::fromJson(obj);
    EXPECT_TRUE(back.completions.has_value());
}

TEST(McpTypesTest, ResourceContentsRoundTripBlob)
{
    ResourceContents contents;
    contents.uri = "file:///tmp/bin.dat";
    contents.mimeType = "application/octet-stream";
    contents.blob = QByteArray("\x01\x02\x03\xff", 4);

    const QJsonObject obj = contents.toJson();
    EXPECT_TRUE(obj.contains("blob"));

    const ResourceContents back = ResourceContents::fromJson(obj);
    EXPECT_EQ(back.uri, contents.uri);
    EXPECT_EQ(back.blob, contents.blob);
}

TEST(McpTypesTest, SamplingMessageAndParamsRoundTrip)
{
    SamplingMessage msg;
    msg.role = "user";
    msg.content = QJsonObject{{"type", "text"}, {"text", "hello"}};

    ModelHint hint;
    hint.name = "claude-sonnet";

    ModelPreferences prefs;
    prefs.hints = {hint};
    prefs.costPriority = 0.25;
    prefs.intelligencePriority = 0.9;

    CreateMessageParams params;
    params.messages = {msg};
    params.modelPreferences = prefs;
    params.systemPrompt = "Be terse.";
    params.includeContext = "thisServer";
    params.temperature = 0.1;
    params.maxTokens = 128;
    params.stopSequences = {"END", "###"};
    params.metadata = QJsonObject{{"source", "unit-test"}};

    const QJsonObject obj = params.toJson();
    EXPECT_EQ(obj.value("maxTokens").toInt(), 128);
    EXPECT_EQ(obj.value("systemPrompt").toString(), "Be terse.");
    EXPECT_EQ(obj.value("includeContext").toString(), "thisServer");
    EXPECT_EQ(obj.value("temperature").toDouble(), 0.1);
    EXPECT_EQ(obj.value("stopSequences").toArray().size(), 2);
    EXPECT_TRUE(obj.contains("modelPreferences"));

    const CreateMessageParams back = CreateMessageParams::fromJson(obj);
    ASSERT_EQ(back.messages.size(), 1);
    EXPECT_EQ(back.messages.first().role, "user");
    EXPECT_EQ(back.messages.first().content.value("text").toString(), "hello");
    EXPECT_EQ(back.systemPrompt, "Be terse.");
    EXPECT_EQ(back.includeContext, "thisServer");
    ASSERT_TRUE(back.temperature.has_value());
    EXPECT_DOUBLE_EQ(*back.temperature, 0.1);
    EXPECT_EQ(back.maxTokens, 128);
    EXPECT_EQ(back.stopSequences, QStringList({"END", "###"}));
    ASSERT_TRUE(back.modelPreferences.has_value());
    ASSERT_EQ(back.modelPreferences->hints.size(), 1);
    EXPECT_EQ(back.modelPreferences->hints.first().name, "claude-sonnet");
    ASSERT_TRUE(back.modelPreferences->costPriority.has_value());
    EXPECT_DOUBLE_EQ(*back.modelPreferences->costPriority, 0.25);
    EXPECT_FALSE(back.modelPreferences->speedPriority.has_value());
}

TEST(McpTypesTest, CreateMessageResultRoundTrip)
{
    CreateMessageResult r;
    r.role = "assistant";
    r.content = QJsonObject{{"type", "text"}, {"text", "world"}};
    r.model = "claude-sonnet-4-6";
    r.stopReason = "endTurn";

    const QJsonObject obj = r.toJson();
    EXPECT_EQ(obj.value("role").toString(), "assistant");
    EXPECT_EQ(obj.value("model").toString(), "claude-sonnet-4-6");
    EXPECT_EQ(obj.value("stopReason").toString(), "endTurn");

    const CreateMessageResult back = CreateMessageResult::fromJson(obj);
    EXPECT_EQ(back.role, "assistant");
    EXPECT_EQ(back.content.value("text").toString(), "world");
    EXPECT_EQ(back.model, "claude-sonnet-4-6");
    EXPECT_EQ(back.stopReason, "endTurn");
}

TEST(McpTypesTest, ClientCapabilitiesPreserveSamplingFlag)
{
    ClientCapabilities caps;
    caps.sampling = SamplingCapability{true};

    const QJsonObject obj = caps.toJson();
    EXPECT_TRUE(obj.contains("sampling"));

    const ClientCapabilities back = ClientCapabilities::fromJson(obj);
    EXPECT_TRUE(back.sampling.has_value());
}

TEST(McpTypesTest, ClientCapabilitiesPreserveElicitationFlag)
{
    ClientCapabilities caps;
    caps.elicitation = ElicitationCapability{true};

    const QJsonObject obj = caps.toJson();
    EXPECT_TRUE(obj.contains("elicitation"));

    const ClientCapabilities back = ClientCapabilities::fromJson(obj);
    EXPECT_TRUE(back.elicitation.has_value());
}

TEST(McpTypesTest, ElicitRequestParamsRoundTripForm)
{
    ElicitRequestParams p;
    p.message = "Enter your GitHub username";
    p.requestedSchema = QJsonObject{
        {"type", "object"},
        {"properties",
         QJsonObject{{"username", QJsonObject{{"type", "string"}}}}},
        {"required", QJsonArray{"username"}}};

    const QJsonObject obj = p.toJson();
    EXPECT_EQ(obj.value("message").toString(), "Enter your GitHub username");
    EXPECT_TRUE(obj.contains("requestedSchema"));
    EXPECT_FALSE(obj.contains("mode"));
    EXPECT_FALSE(obj.contains("url"));

    const ElicitRequestParams back = ElicitRequestParams::fromJson(obj);
    EXPECT_EQ(back.message, p.message);
    EXPECT_EQ(
        back.requestedSchema.value("properties")
            .toObject()
            .value("username")
            .toObject()
            .value("type")
            .toString(),
        "string");
    EXPECT_TRUE(back.mode.isEmpty());
    EXPECT_TRUE(back.url.isEmpty());
}

TEST(McpTypesTest, ElicitRequestParamsRoundTripUrlMode)
{
    ElicitRequestParams p;
    p.message = "Complete login in your browser";
    p.mode = "url";
    p.url = "https://example.com/auth?state=abc";

    const QJsonObject obj = p.toJson();
    EXPECT_EQ(obj.value("mode").toString(), "url");
    EXPECT_EQ(obj.value("url").toString(), p.url);
    EXPECT_FALSE(obj.contains("requestedSchema"));

    const ElicitRequestParams back = ElicitRequestParams::fromJson(obj);
    EXPECT_EQ(back.mode, "url");
    EXPECT_EQ(back.url, p.url);
}

TEST(McpTypesTest, ElicitResultAcceptRoundTripsContent)
{
    ElicitResult r;
    r.action = ElicitAction::Accept;
    r.content = QJsonObject{{"username", "octocat"}, {"remember", true}};

    const QJsonObject obj = r.toJson();
    EXPECT_EQ(obj.value("action").toString(), "accept");
    EXPECT_EQ(obj.value("content").toObject().value("username").toString(), "octocat");

    const ElicitResult back = ElicitResult::fromJson(obj);
    EXPECT_EQ(back.action, "accept");
    EXPECT_EQ(back.content.value("remember").toBool(), true);
}

TEST(McpTypesTest, ElicitResultDeclineOmitsContent)
{
    ElicitResult r;
    r.action = ElicitAction::Decline;
    // Even if a caller leaves stale content here, the decline envelope
    // MUST NOT ship it — the spec guidance is that `content` is only
    // meaningful for "accept".
    r.content = QJsonObject{{"stale", "value"}};

    const QJsonObject obj = r.toJson();
    EXPECT_EQ(obj.value("action").toString(), "decline");
    EXPECT_FALSE(obj.contains("content"));
}
