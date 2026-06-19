// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/AcpTypes.hpp>

namespace LLMQore::Acp {

namespace {

void insertIfNonEmpty(QJsonObject &o, const char *key, const QString &v)
{
    if (!v.isEmpty())
        o.insert(QLatin1String(key), v);
}

QStringList stringListFromJson(const QJsonArray &arr)
{
    QStringList out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.append(v.toString());
    return out;
}

QJsonArray stringListToJson(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &s : list)
        arr.append(s);
    return arr;
}

} // namespace

// --- EnvVariable ---

QJsonObject EnvVariable::toJson() const
{
    return QJsonObject{{"name", name}, {"value", value}};
}

EnvVariable EnvVariable::fromJson(const QJsonObject &obj)
{
    EnvVariable e;
    e.name = obj.value("name").toString();
    e.value = obj.value("value").toString();
    return e;
}

QJsonArray envToJson(const QList<EnvVariable> &env)
{
    QJsonArray arr;
    for (const EnvVariable &e : env)
        arr.append(e.toJson());
    return arr;
}

QList<EnvVariable> envFromJson(const QJsonArray &arr)
{
    QList<EnvVariable> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.append(EnvVariable::fromJson(v.toObject()));
    return out;
}

// --- Implementation ---

QJsonObject Implementation::toJson() const
{
    QJsonObject o{{"name", name}, {"version", version}};
    insertIfNonEmpty(o, "title", title);
    return o;
}

Implementation Implementation::fromJson(const QJsonObject &obj)
{
    Implementation i;
    i.name = obj.value("name").toString();
    i.version = obj.value("version").toString();
    i.title = obj.value("title").toString();
    return i;
}

// --- FileSystemCapability ---

QJsonObject FileSystemCapability::toJson() const
{
    return QJsonObject{{"readTextFile", readTextFile}, {"writeTextFile", writeTextFile}};
}

FileSystemCapability FileSystemCapability::fromJson(const QJsonObject &obj)
{
    FileSystemCapability c;
    c.readTextFile = obj.value("readTextFile").toBool();
    c.writeTextFile = obj.value("writeTextFile").toBool();
    return c;
}

// --- ClientCapabilities ---

QJsonObject ClientCapabilities::toJson() const
{
    return QJsonObject{{"fs", fs.toJson()}, {"terminal", terminal}};
}

ClientCapabilities ClientCapabilities::fromJson(const QJsonObject &obj)
{
    ClientCapabilities c;
    c.fs = FileSystemCapability::fromJson(obj.value("fs").toObject());
    c.terminal = obj.value("terminal").toBool();
    return c;
}

// --- PromptCapabilities ---

QJsonObject PromptCapabilities::toJson() const
{
    return QJsonObject{
        {"image", image}, {"audio", audio}, {"embeddedContext", embeddedContext}};
}

PromptCapabilities PromptCapabilities::fromJson(const QJsonObject &obj)
{
    PromptCapabilities c;
    c.image = obj.value("image").toBool();
    c.audio = obj.value("audio").toBool();
    c.embeddedContext = obj.value("embeddedContext").toBool();
    return c;
}

// --- McpCapabilities ---

QJsonObject McpCapabilities::toJson() const
{
    return QJsonObject{{"http", http}, {"sse", sse}};
}

McpCapabilities McpCapabilities::fromJson(const QJsonObject &obj)
{
    McpCapabilities c;
    c.http = obj.value("http").toBool();
    c.sse = obj.value("sse").toBool();
    return c;
}

// --- AgentCapabilities ---

QJsonObject AgentCapabilities::toJson() const
{
    return QJsonObject{
        {"loadSession", loadSession},
        {"promptCapabilities", promptCapabilities.toJson()},
        {"mcpCapabilities", mcpCapabilities.toJson()},
    };
}

AgentCapabilities AgentCapabilities::fromJson(const QJsonObject &obj)
{
    AgentCapabilities c;
    c.loadSession = obj.value("loadSession").toBool();
    c.promptCapabilities
        = PromptCapabilities::fromJson(obj.value("promptCapabilities").toObject());
    c.mcpCapabilities = McpCapabilities::fromJson(obj.value("mcpCapabilities").toObject());
    return c;
}

// --- AuthMethod ---

QJsonObject AuthMethod::toJson() const
{
    QJsonObject o{{"id", id}, {"name", name}};
    insertIfNonEmpty(o, "description", description);
    return o;
}

AuthMethod AuthMethod::fromJson(const QJsonObject &obj)
{
    AuthMethod m;
    m.id = obj.value("id").toString();
    m.name = obj.value("name").toString();
    m.description = obj.value("description").toString();
    return m;
}

// --- InitializeParams ---

QJsonObject InitializeParams::toJson() const
{
    QJsonObject o{
        {"protocolVersion", protocolVersion},
        {"clientCapabilities", clientCapabilities.toJson()},
    };
    if (clientInfo)
        o.insert("clientInfo", clientInfo->toJson());
    return o;
}

InitializeParams InitializeParams::fromJson(const QJsonObject &obj)
{
    InitializeParams p;
    p.protocolVersion = obj.value("protocolVersion").toInt(kAcpProtocolVersion);
    p.clientCapabilities
        = ClientCapabilities::fromJson(obj.value("clientCapabilities").toObject());
    if (obj.contains("clientInfo"))
        p.clientInfo = Implementation::fromJson(obj.value("clientInfo").toObject());
    return p;
}

// --- InitializeResult ---

QJsonObject InitializeResult::toJson() const
{
    QJsonObject o{
        {"protocolVersion", protocolVersion},
        {"agentCapabilities", agentCapabilities.toJson()},
    };
    QJsonArray methods;
    for (const AuthMethod &m : authMethods)
        methods.append(m.toJson());
    o.insert("authMethods", methods);
    if (agentInfo)
        o.insert("agentInfo", agentInfo->toJson());
    return o;
}

InitializeResult InitializeResult::fromJson(const QJsonObject &obj)
{
    InitializeResult r;
    r.protocolVersion = obj.value("protocolVersion").toInt(kAcpProtocolVersion);
    r.agentCapabilities
        = AgentCapabilities::fromJson(obj.value("agentCapabilities").toObject());
    for (const QJsonValue &v : obj.value("authMethods").toArray())
        r.authMethods.append(AuthMethod::fromJson(v.toObject()));
    if (obj.contains("agentInfo"))
        r.agentInfo = Implementation::fromJson(obj.value("agentInfo").toObject());
    return r;
}

// --- McpServer ---

QJsonObject McpServer::toJson() const
{
    return QJsonObject{
        {"name", name},
        {"command", command},
        {"args", stringListToJson(args)},
        {"env", envToJson(env)},
    };
}

McpServer McpServer::fromJson(const QJsonObject &obj)
{
    McpServer s;
    s.name = obj.value("name").toString();
    s.command = obj.value("command").toString();
    s.args = stringListFromJson(obj.value("args").toArray());
    s.env = envFromJson(obj.value("env").toArray());
    return s;
}

// --- SessionMode ---

QJsonObject SessionMode::toJson() const
{
    QJsonObject o{{"id", id}, {"name", name}};
    insertIfNonEmpty(o, "description", description);
    return o;
}

SessionMode SessionMode::fromJson(const QJsonObject &obj)
{
    SessionMode m;
    m.id = obj.value("id").toString();
    m.name = obj.value("name").toString();
    m.description = obj.value("description").toString();
    return m;
}

// --- SessionModeState ---

QJsonObject SessionModeState::toJson() const
{
    QJsonArray modes;
    for (const SessionMode &m : availableModes)
        modes.append(m.toJson());
    return QJsonObject{{"currentModeId", currentModeId}, {"availableModes", modes}};
}

SessionModeState SessionModeState::fromJson(const QJsonObject &obj)
{
    SessionModeState s;
    s.currentModeId = obj.value("currentModeId").toString();
    for (const QJsonValue &v : obj.value("availableModes").toArray())
        s.availableModes.append(SessionMode::fromJson(v.toObject()));
    return s;
}

// --- NewSessionParams ---

namespace {

QJsonArray mcpServersToJson(const QList<McpServer> &servers)
{
    QJsonArray arr;
    for (const McpServer &s : servers)
        arr.append(s.toJson());
    return arr;
}

QList<McpServer> mcpServersFromJson(const QJsonArray &arr)
{
    QList<McpServer> out;
    for (const QJsonValue &v : arr)
        out.append(McpServer::fromJson(v.toObject()));
    return out;
}

} // namespace

QJsonObject NewSessionParams::toJson() const
{
    QJsonObject o{{"cwd", cwd}, {"mcpServers", mcpServersToJson(mcpServers)}};
    if (!additionalDirectories.isEmpty())
        o.insert("additionalDirectories", stringListToJson(additionalDirectories));
    return o;
}

NewSessionParams NewSessionParams::fromJson(const QJsonObject &obj)
{
    NewSessionParams p;
    p.cwd = obj.value("cwd").toString();
    p.mcpServers = mcpServersFromJson(obj.value("mcpServers").toArray());
    p.additionalDirectories
        = stringListFromJson(obj.value("additionalDirectories").toArray());
    return p;
}

// --- NewSessionResult ---

QJsonObject NewSessionResult::toJson() const
{
    QJsonObject o{{"sessionId", sessionId}};
    if (modes)
        o.insert("modes", modes->toJson());
    return o;
}

NewSessionResult NewSessionResult::fromJson(const QJsonObject &obj)
{
    NewSessionResult r;
    r.sessionId = obj.value("sessionId").toString();
    if (obj.contains("modes") && obj.value("modes").isObject())
        r.modes = SessionModeState::fromJson(obj.value("modes").toObject());
    return r;
}

// --- LoadSessionParams ---

QJsonObject LoadSessionParams::toJson() const
{
    QJsonObject o{
        {"sessionId", sessionId},
        {"cwd", cwd},
        {"mcpServers", mcpServersToJson(mcpServers)},
    };
    if (!additionalDirectories.isEmpty())
        o.insert("additionalDirectories", stringListToJson(additionalDirectories));
    return o;
}

LoadSessionParams LoadSessionParams::fromJson(const QJsonObject &obj)
{
    LoadSessionParams p;
    p.sessionId = obj.value("sessionId").toString();
    p.cwd = obj.value("cwd").toString();
    p.mcpServers = mcpServersFromJson(obj.value("mcpServers").toArray());
    p.additionalDirectories
        = stringListFromJson(obj.value("additionalDirectories").toArray());
    return p;
}

// --- EmbeddedResource ---

QJsonObject EmbeddedResource::toJson() const
{
    QJsonObject o{{"uri", uri}};
    insertIfNonEmpty(o, "mimeType", mimeType);
    if (!blob.isEmpty())
        o.insert("blob", blob);
    else
        o.insert("text", text);
    return o;
}

EmbeddedResource EmbeddedResource::fromJson(const QJsonObject &obj)
{
    EmbeddedResource r;
    r.uri = obj.value("uri").toString();
    r.mimeType = obj.value("mimeType").toString();
    r.text = obj.value("text").toString();
    r.blob = obj.value("blob").toString();
    return r;
}

// --- ContentBlock ---

QJsonObject ContentBlock::toJson() const
{
    QJsonObject o{{"type", type}};
    if (type == QLatin1String("text")) {
        o.insert("text", text);
    } else if (type == QLatin1String("image")) {
        o.insert("data", data);
        o.insert("mimeType", mimeType);
        insertIfNonEmpty(o, "uri", uri);
    } else if (type == QLatin1String("audio")) {
        o.insert("data", data);
        o.insert("mimeType", mimeType);
    } else if (type == QLatin1String("resource_link")) {
        o.insert("uri", uri);
        o.insert("name", name);
        insertIfNonEmpty(o, "description", description);
        insertIfNonEmpty(o, "mimeType", mimeType);
        insertIfNonEmpty(o, "title", title);
        if (size)
            o.insert("size", *size);
    } else if (type == QLatin1String("resource")) {
        if (resource)
            o.insert("resource", resource->toJson());
    }
    if (!annotations.isEmpty())
        o.insert("annotations", annotations);
    return o;
}

ContentBlock ContentBlock::fromJson(const QJsonObject &obj)
{
    ContentBlock b;
    b.type = obj.value("type").toString();
    b.text = obj.value("text").toString();
    b.data = obj.value("data").toString();
    b.mimeType = obj.value("mimeType").toString();
    b.uri = obj.value("uri").toString();
    b.name = obj.value("name").toString();
    b.description = obj.value("description").toString();
    b.title = obj.value("title").toString();
    if (obj.contains("size"))
        b.size = obj.value("size").toInt();
    if (obj.contains("resource") && obj.value("resource").isObject())
        b.resource = EmbeddedResource::fromJson(obj.value("resource").toObject());
    b.annotations = obj.value("annotations").toObject();
    return b;
}

ContentBlock ContentBlock::makeText(const QString &text)
{
    ContentBlock b;
    b.type = QStringLiteral("text");
    b.text = text;
    return b;
}

QJsonArray contentBlocksToJson(const QList<ContentBlock> &blocks)
{
    QJsonArray arr;
    for (const ContentBlock &b : blocks)
        arr.append(b.toJson());
    return arr;
}

QList<ContentBlock> contentBlocksFromJson(const QJsonArray &arr)
{
    QList<ContentBlock> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.append(ContentBlock::fromJson(v.toObject()));
    return out;
}

// --- PromptParams ---

QJsonObject PromptParams::toJson() const
{
    return QJsonObject{{"sessionId", sessionId}, {"prompt", contentBlocksToJson(prompt)}};
}

PromptParams PromptParams::fromJson(const QJsonObject &obj)
{
    PromptParams p;
    p.sessionId = obj.value("sessionId").toString();
    p.prompt = contentBlocksFromJson(obj.value("prompt").toArray());
    return p;
}

// --- PromptResult ---

QJsonObject PromptResult::toJson() const
{
    return QJsonObject{{"stopReason", stopReason}};
}

PromptResult PromptResult::fromJson(const QJsonObject &obj)
{
    PromptResult r;
    r.stopReason = obj.value("stopReason").toString(StopReason::EndTurn);
    return r;
}

// --- ToolCallLocation ---

QJsonObject ToolCallLocation::toJson() const
{
    QJsonObject o{{"path", path}};
    if (line)
        o.insert("line", *line);
    return o;
}

ToolCallLocation ToolCallLocation::fromJson(const QJsonObject &obj)
{
    ToolCallLocation l;
    l.path = obj.value("path").toString();
    if (obj.contains("line"))
        l.line = obj.value("line").toInt();
    return l;
}

// --- ToolCallContent ---

QJsonObject ToolCallContent::toJson() const
{
    QJsonObject o{{"type", type}};
    if (type == QLatin1String("content")) {
        if (content)
            o.insert("content", content->toJson());
    } else if (type == QLatin1String("diff")) {
        o.insert("path", path);
        if (!oldText.isNull())
            o.insert("oldText", oldText);
        o.insert("newText", newText);
    } else if (type == QLatin1String("terminal")) {
        o.insert("terminalId", terminalId);
    }
    return o;
}

ToolCallContent ToolCallContent::fromJson(const QJsonObject &obj)
{
    ToolCallContent c;
    c.type = obj.value("type").toString(QStringLiteral("content"));
    if (obj.contains("content") && obj.value("content").isObject())
        c.content = ContentBlock::fromJson(obj.value("content").toObject());
    c.path = obj.value("path").toString();
    c.oldText = obj.value("oldText").toString();
    c.newText = obj.value("newText").toString();
    c.terminalId = obj.value("terminalId").toString();
    return c;
}

// --- ToolCall ---

QJsonObject ToolCall::toJson() const
{
    QJsonObject o{{"toolCallId", toolCallId}};
    insertIfNonEmpty(o, "title", title);
    insertIfNonEmpty(o, "kind", kind);
    insertIfNonEmpty(o, "status", status);
    if (!content.isEmpty()) {
        QJsonArray arr;
        for (const ToolCallContent &c : content)
            arr.append(c.toJson());
        o.insert("content", arr);
    }
    if (!locations.isEmpty()) {
        QJsonArray arr;
        for (const ToolCallLocation &l : locations)
            arr.append(l.toJson());
        o.insert("locations", arr);
    }
    if (!rawInput.isEmpty())
        o.insert("rawInput", rawInput);
    if (!rawOutput.isEmpty())
        o.insert("rawOutput", rawOutput);
    return o;
}

ToolCall ToolCall::fromJson(const QJsonObject &obj)
{
    ToolCall t;
    t.toolCallId = obj.value("toolCallId").toString();
    t.title = obj.value("title").toString();
    t.kind = obj.value("kind").toString();
    t.status = obj.value("status").toString();
    for (const QJsonValue &v : obj.value("content").toArray())
        t.content.append(ToolCallContent::fromJson(v.toObject()));
    for (const QJsonValue &v : obj.value("locations").toArray())
        t.locations.append(ToolCallLocation::fromJson(v.toObject()));
    t.rawInput = obj.value("rawInput").toObject();
    t.rawOutput = obj.value("rawOutput").toObject();
    return t;
}

// --- PlanEntry ---

QJsonObject PlanEntry::toJson() const
{
    return QJsonObject{{"content", content}, {"priority", priority}, {"status", status}};
}

PlanEntry PlanEntry::fromJson(const QJsonObject &obj)
{
    PlanEntry e;
    e.content = obj.value("content").toString();
    e.priority = obj.value("priority").toString();
    e.status = obj.value("status").toString();
    return e;
}

// --- Plan ---

QJsonObject Plan::toJson() const
{
    QJsonArray arr;
    for (const PlanEntry &e : entries)
        arr.append(e.toJson());
    return QJsonObject{{"entries", arr}};
}

Plan Plan::fromJson(const QJsonObject &obj)
{
    Plan p;
    for (const QJsonValue &v : obj.value("entries").toArray())
        p.entries.append(PlanEntry::fromJson(v.toObject()));
    return p;
}

// --- AvailableCommand ---

QJsonObject AvailableCommand::toJson() const
{
    QJsonObject o{{"name", name}, {"description", description}};
    if (!inputHint.isEmpty())
        o.insert("input", QJsonObject{{"hint", inputHint}});
    return o;
}

AvailableCommand AvailableCommand::fromJson(const QJsonObject &obj)
{
    AvailableCommand c;
    c.name = obj.value("name").toString();
    c.description = obj.value("description").toString();
    c.inputHint = obj.value("input").toObject().value("hint").toString();
    return c;
}

// --- SessionUpdate ---

QJsonObject SessionUpdate::toJson() const
{
    QJsonObject o{{"sessionUpdate", sessionUpdate}};
    if (sessionUpdate == QLatin1String(SessionUpdateKind::UserMessageChunk)
        || sessionUpdate == QLatin1String(SessionUpdateKind::AgentMessageChunk)
        || sessionUpdate == QLatin1String(SessionUpdateKind::AgentThoughtChunk)) {
        if (content)
            o.insert("content", content->toJson());
    } else if (
        sessionUpdate == QLatin1String(SessionUpdateKind::ToolCall)
        || sessionUpdate == QLatin1String(SessionUpdateKind::ToolCallUpdate)) {
        if (toolCall)
            o.insert("toolCall", toolCall->toJson());
    } else if (sessionUpdate == QLatin1String(SessionUpdateKind::Plan)) {
        if (plan)
            o.insert("plan", plan->toJson());
    } else if (sessionUpdate == QLatin1String(SessionUpdateKind::AvailableCommandsUpdate)) {
        QJsonArray arr;
        for (const AvailableCommand &c : availableCommands)
            arr.append(c.toJson());
        o.insert("availableCommands", arr);
    } else if (sessionUpdate == QLatin1String(SessionUpdateKind::CurrentModeUpdate)) {
        o.insert("currentModeId", currentModeId);
    }
    return o;
}

SessionUpdate SessionUpdate::fromJson(const QJsonObject &obj)
{
    SessionUpdate u;
    u.sessionUpdate = obj.value("sessionUpdate").toString();
    if (obj.contains("content") && obj.value("content").isObject())
        u.content = ContentBlock::fromJson(obj.value("content").toObject());
    if (obj.contains("toolCall") && obj.value("toolCall").isObject())
        u.toolCall = ToolCall::fromJson(obj.value("toolCall").toObject());
    if (obj.contains("plan") && obj.value("plan").isObject())
        u.plan = Plan::fromJson(obj.value("plan").toObject());
    for (const QJsonValue &v : obj.value("availableCommands").toArray())
        u.availableCommands.append(AvailableCommand::fromJson(v.toObject()));
    u.currentModeId = obj.value("currentModeId").toString();
    return u;
}

// --- SessionNotification ---

QJsonObject SessionNotification::toJson() const
{
    return QJsonObject{{"sessionId", sessionId}, {"update", update.toJson()}};
}

SessionNotification SessionNotification::fromJson(const QJsonObject &obj)
{
    SessionNotification n;
    n.sessionId = obj.value("sessionId").toString();
    n.update = SessionUpdate::fromJson(obj.value("update").toObject());
    return n;
}

// --- PermissionOption ---

QJsonObject PermissionOption::toJson() const
{
    return QJsonObject{{"optionId", optionId}, {"name", name}, {"kind", kind}};
}

PermissionOption PermissionOption::fromJson(const QJsonObject &obj)
{
    PermissionOption o;
    o.optionId = obj.value("optionId").toString();
    o.name = obj.value("name").toString();
    o.kind = obj.value("kind").toString();
    return o;
}

// --- RequestPermissionParams ---

QJsonObject RequestPermissionParams::toJson() const
{
    QJsonArray opts;
    for (const PermissionOption &o : options)
        opts.append(o.toJson());
    return QJsonObject{
        {"sessionId", sessionId},
        {"toolCall", toolCall.toJson()},
        {"options", opts},
    };
}

RequestPermissionParams RequestPermissionParams::fromJson(const QJsonObject &obj)
{
    RequestPermissionParams p;
    p.sessionId = obj.value("sessionId").toString();
    p.toolCall = ToolCall::fromJson(obj.value("toolCall").toObject());
    for (const QJsonValue &v : obj.value("options").toArray())
        p.options.append(PermissionOption::fromJson(v.toObject()));
    return p;
}

// --- RequestPermissionResult ---
//
// Wire shape: { "outcome": { "outcome": "selected", "optionId": "..." } }
//          or { "outcome": { "outcome": "cancelled" } }

QJsonObject RequestPermissionResult::toJson() const
{
    QJsonObject inner{{"outcome", outcome}};
    if (outcome == QLatin1String("selected"))
        inner.insert("optionId", optionId);
    return QJsonObject{{"outcome", inner}};
}

RequestPermissionResult RequestPermissionResult::fromJson(const QJsonObject &obj)
{
    RequestPermissionResult r;
    const QJsonObject inner = obj.value("outcome").toObject();
    r.outcome = inner.value("outcome").toString();
    r.optionId = inner.value("optionId").toString();
    return r;
}

RequestPermissionResult RequestPermissionResult::selected(const QString &optionId)
{
    RequestPermissionResult r;
    r.outcome = QStringLiteral("selected");
    r.optionId = optionId;
    return r;
}

RequestPermissionResult RequestPermissionResult::cancelled()
{
    RequestPermissionResult r;
    r.outcome = QStringLiteral("cancelled");
    return r;
}

// --- ReadTextFileParams ---

QJsonObject ReadTextFileParams::toJson() const
{
    QJsonObject o{{"sessionId", sessionId}, {"path", path}};
    if (line)
        o.insert("line", *line);
    if (limit)
        o.insert("limit", *limit);
    return o;
}

ReadTextFileParams ReadTextFileParams::fromJson(const QJsonObject &obj)
{
    ReadTextFileParams p;
    p.sessionId = obj.value("sessionId").toString();
    p.path = obj.value("path").toString();
    if (obj.contains("line") && !obj.value("line").isNull())
        p.line = obj.value("line").toInt();
    if (obj.contains("limit") && !obj.value("limit").isNull())
        p.limit = obj.value("limit").toInt();
    return p;
}

// --- ReadTextFileResult ---

QJsonObject ReadTextFileResult::toJson() const
{
    return QJsonObject{{"content", content}};
}

ReadTextFileResult ReadTextFileResult::fromJson(const QJsonObject &obj)
{
    ReadTextFileResult r;
    r.content = obj.value("content").toString();
    return r;
}

// --- WriteTextFileParams ---

QJsonObject WriteTextFileParams::toJson() const
{
    return QJsonObject{{"sessionId", sessionId}, {"path", path}, {"content", content}};
}

WriteTextFileParams WriteTextFileParams::fromJson(const QJsonObject &obj)
{
    WriteTextFileParams p;
    p.sessionId = obj.value("sessionId").toString();
    p.path = obj.value("path").toString();
    p.content = obj.value("content").toString();
    return p;
}

// --- CreateTerminalParams ---

QJsonObject CreateTerminalParams::toJson() const
{
    QJsonObject o{
        {"sessionId", sessionId},
        {"command", command},
        {"args", stringListToJson(args)},
        {"env", envToJson(env)},
    };
    insertIfNonEmpty(o, "cwd", cwd);
    if (outputByteLimit)
        o.insert("outputByteLimit", *outputByteLimit);
    return o;
}

CreateTerminalParams CreateTerminalParams::fromJson(const QJsonObject &obj)
{
    CreateTerminalParams p;
    p.sessionId = obj.value("sessionId").toString();
    p.command = obj.value("command").toString();
    p.args = stringListFromJson(obj.value("args").toArray());
    p.cwd = obj.value("cwd").toString();
    p.env = envFromJson(obj.value("env").toArray());
    if (obj.contains("outputByteLimit") && !obj.value("outputByteLimit").isNull())
        p.outputByteLimit = obj.value("outputByteLimit").toInt();
    return p;
}

// --- CreateTerminalResult ---

QJsonObject CreateTerminalResult::toJson() const
{
    return QJsonObject{{"terminalId", terminalId}};
}

CreateTerminalResult CreateTerminalResult::fromJson(const QJsonObject &obj)
{
    CreateTerminalResult r;
    r.terminalId = obj.value("terminalId").toString();
    return r;
}

// --- ExitStatus ---

QJsonObject ExitStatus::toJson() const
{
    QJsonObject o;
    o.insert("exitCode", exitCode ? QJsonValue(*exitCode) : QJsonValue());
    o.insert("signal", signal.isEmpty() ? QJsonValue() : QJsonValue(signal));
    return o;
}

ExitStatus ExitStatus::fromJson(const QJsonObject &obj)
{
    ExitStatus s;
    if (obj.contains("exitCode") && !obj.value("exitCode").isNull())
        s.exitCode = obj.value("exitCode").toInt();
    s.signal = obj.value("signal").toString();
    return s;
}

// --- TerminalOutputParams ---

QJsonObject TerminalOutputParams::toJson() const
{
    return QJsonObject{{"sessionId", sessionId}, {"terminalId", terminalId}};
}

TerminalOutputParams TerminalOutputParams::fromJson(const QJsonObject &obj)
{
    TerminalOutputParams p;
    p.sessionId = obj.value("sessionId").toString();
    p.terminalId = obj.value("terminalId").toString();
    return p;
}

// --- TerminalOutputResult ---

QJsonObject TerminalOutputResult::toJson() const
{
    QJsonObject o{{"output", output}, {"truncated", truncated}};
    if (exitStatus)
        o.insert("exitStatus", exitStatus->toJson());
    return o;
}

TerminalOutputResult TerminalOutputResult::fromJson(const QJsonObject &obj)
{
    TerminalOutputResult r;
    r.output = obj.value("output").toString();
    r.truncated = obj.value("truncated").toBool();
    if (obj.contains("exitStatus") && obj.value("exitStatus").isObject())
        r.exitStatus = ExitStatus::fromJson(obj.value("exitStatus").toObject());
    return r;
}

// --- TerminalRefParams ---

QJsonObject TerminalRefParams::toJson() const
{
    return QJsonObject{{"sessionId", sessionId}, {"terminalId", terminalId}};
}

TerminalRefParams TerminalRefParams::fromJson(const QJsonObject &obj)
{
    TerminalRefParams p;
    p.sessionId = obj.value("sessionId").toString();
    p.terminalId = obj.value("terminalId").toString();
    return p;
}

// --- WaitForTerminalExitResult ---

QJsonObject WaitForTerminalExitResult::toJson() const
{
    QJsonObject o;
    o.insert("exitCode", exitCode ? QJsonValue(*exitCode) : QJsonValue());
    o.insert("signal", signal.isEmpty() ? QJsonValue() : QJsonValue(signal));
    return o;
}

WaitForTerminalExitResult WaitForTerminalExitResult::fromJson(const QJsonObject &obj)
{
    WaitForTerminalExitResult r;
    if (obj.contains("exitCode") && !obj.value("exitCode").isNull())
        r.exitCode = obj.value("exitCode").toInt();
    r.signal = obj.value("signal").toString();
    return r;
}

} // namespace LLMQore::Acp
