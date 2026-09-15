// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/AcpTypes.hpp>

#include "AcpTypeSchema.hpp"

namespace LLMQore::Acp {

namespace {

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

template<typename... Ts>
void registerAll(const std::tuple<Ts...> *)
{
    (qRegisterMetaType<Ts>(), ...);
}

} // namespace

void registerMetatypes()
{
    static const bool once = []() {
        registerAll(static_cast<const QueuedTypes *>(nullptr));
        return true;
    }();
    Q_UNUSED(once)
}

QJsonObject EnvVariable::toJson() const
{
    return Json::toJson(*this);
}

EnvVariable EnvVariable::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<EnvVariable>(obj);
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

QJsonObject Implementation::toJson() const
{
    return Json::toJson(*this);
}

Implementation Implementation::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<Implementation>(obj);
}

QJsonObject FileSystemCapability::toJson() const
{
    return Json::toJson(*this);
}

FileSystemCapability FileSystemCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<FileSystemCapability>(obj);
}

QJsonObject ClientCapabilities::toJson() const
{
    return Json::toJson(*this);
}

ClientCapabilities ClientCapabilities::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ClientCapabilities>(obj);
}

QJsonObject PromptCapabilities::toJson() const
{
    return Json::toJson(*this);
}

PromptCapabilities PromptCapabilities::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PromptCapabilities>(obj);
}

QJsonObject McpCapabilities::toJson() const
{
    return Json::toJson(*this);
}

McpCapabilities McpCapabilities::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<McpCapabilities>(obj);
}

QJsonObject AgentCapabilities::toJson() const
{
    return Json::toJson(*this);
}

AgentCapabilities AgentCapabilities::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<AgentCapabilities>(obj);
}

QJsonObject AuthMethod::toJson() const
{
    return Json::toJson(*this);
}

AuthMethod AuthMethod::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<AuthMethod>(obj);
}

QJsonObject InitializeParams::toJson() const
{
    return Json::toJson(*this);
}

InitializeParams InitializeParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<InitializeParams>(obj);
}

QJsonObject InitializeResult::toJson() const
{
    return Json::toJson(*this);
}

InitializeResult InitializeResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<InitializeResult>(obj);
}

QJsonObject McpServer::toJson() const
{
    if (isStdio()) {
        return QJsonObject{
            {"name", name},
            {"command", command},
            {"args", stringListToJson(args)},
            {"env", envToJson(env)},
        };
    }
    return QJsonObject{
        {"type", type},
        {"name", name},
        {"url", url},
        {"headers", envToJson(headers)},
    };
}

McpServer McpServer::fromJson(const QJsonObject &obj)
{
    McpServer s;
    s.type = obj.value("type").toString();
    s.name = obj.value("name").toString();
    if (s.isStdio()) {
        s.command = obj.value("command").toString();
        s.args = stringListFromJson(obj.value("args").toArray());
        s.env = envFromJson(obj.value("env").toArray());
    } else {
        s.url = obj.value("url").toString();
        s.headers = envFromJson(obj.value("headers").toArray());
    }
    return s;
}

McpServer McpServer::stdio(
    const QString &name,
    const QString &command,
    const QStringList &args,
    const QList<EnvVariable> &env)
{
    McpServer s;
    s.name = name;
    s.command = command;
    s.args = args;
    s.env = env;
    return s;
}

McpServer McpServer::http(
    const QString &name, const QString &url, const QList<EnvVariable> &headers)
{
    McpServer s;
    s.type = QStringLiteral("http");
    s.name = name;
    s.url = url;
    s.headers = headers;
    return s;
}

McpServer McpServer::sse(
    const QString &name, const QString &url, const QList<EnvVariable> &headers)
{
    McpServer s;
    s.type = QStringLiteral("sse");
    s.name = name;
    s.url = url;
    s.headers = headers;
    return s;
}

QJsonObject SessionMode::toJson() const
{
    return Json::toJson(*this);
}

SessionMode SessionMode::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<SessionMode>(obj);
}

QJsonObject SessionModeState::toJson() const
{
    return Json::toJson(*this);
}

SessionModeState SessionModeState::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<SessionModeState>(obj);
}

QJsonObject SessionConfigSelectOption::toJson() const
{
    return Json::toJson(*this);
}

SessionConfigSelectOption SessionConfigSelectOption::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<SessionConfigSelectOption>(obj);
}

QJsonObject SessionConfigSelectGroup::toJson() const
{
    return Json::toJson(*this);
}

SessionConfigSelectGroup SessionConfigSelectGroup::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<SessionConfigSelectGroup>(obj);
}

QJsonObject SessionConfigOption::toJson() const
{
    QJsonObject o{{"id", id}, {"name", name}, {"type", type}};
    if (!description.isEmpty())
        o.insert("description", description);
    if (!category.isEmpty())
        o.insert("category", category);
    if (type == QLatin1String("boolean"))
        o.insert("currentValue", enabled);
    else
        o.insert("currentValue", value);
    if (!options.isEmpty()) {
        QJsonArray arr;
        for (const SessionConfigSelectOption &v : options)
            arr.append(v.toJson());
        o.insert("options", arr);
    } else if (!groups.isEmpty()) {
        QJsonArray arr;
        for (const SessionConfigSelectGroup &g : groups)
            arr.append(g.toJson());
        o.insert("options", arr);
    }
    return o;
}

SessionConfigOption SessionConfigOption::fromJson(const QJsonObject &obj)
{
    SessionConfigOption o;
    o.id = obj.value("id").toString();
    o.name = obj.value("name").toString();
    o.description = obj.value("description").toString();
    o.category = obj.value("category").toString();
    o.type = obj.value("type").toString();
    if (o.type == QLatin1String("boolean"))
        o.enabled = obj.value("currentValue").toBool();
    else
        o.value = obj.value("currentValue").toString();
    for (const QJsonValue &v : obj.value("options").toArray()) {
        const QJsonObject entry = v.toObject();
        if (entry.contains("options"))
            o.groups.append(SessionConfigSelectGroup::fromJson(entry));
        else
            o.options.append(SessionConfigSelectOption::fromJson(entry));
    }
    return o;
}

QJsonArray configOptionsToJson(const QList<SessionConfigOption> &options)
{
    QJsonArray arr;
    for (const SessionConfigOption &o : options)
        arr.append(o.toJson());
    return arr;
}

QList<SessionConfigOption> configOptionsFromJson(const QJsonArray &arr)
{
    QList<SessionConfigOption> options;
    for (const QJsonValue &v : arr)
        options.append(SessionConfigOption::fromJson(v.toObject()));
    return options;
}

QJsonObject NewSessionParams::toJson() const
{
    return Json::toJson(*this);
}

NewSessionParams NewSessionParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<NewSessionParams>(obj);
}

QJsonObject NewSessionResult::toJson() const
{
    return Json::toJson(*this);
}

NewSessionResult NewSessionResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<NewSessionResult>(obj);
}

QJsonObject LoadSessionParams::toJson() const
{
    return Json::toJson(*this);
}

LoadSessionParams LoadSessionParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<LoadSessionParams>(obj);
}

QJsonObject EmbeddedResource::toJson() const
{
    QJsonObject o{{"uri", uri}};
    if (!mimeType.isEmpty())
        o.insert("mimeType", mimeType);
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

QJsonObject ContentBlock::toJson() const
{
    QJsonObject o{{"type", type}};
    if (type == QLatin1String("text")) {
        o.insert("text", text);
    } else if (type == QLatin1String("image")) {
        o.insert("data", data);
        o.insert("mimeType", mimeType);
        if (!uri.isEmpty())
            o.insert("uri", uri);
    } else if (type == QLatin1String("audio")) {
        o.insert("data", data);
        o.insert("mimeType", mimeType);
    } else if (type == QLatin1String("resource_link")) {
        o.insert("uri", uri);
        o.insert("name", name);
        if (!description.isEmpty())
            o.insert("description", description);
        if (!mimeType.isEmpty())
            o.insert("mimeType", mimeType);
        if (!title.isEmpty())
            o.insert("title", title);
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
    return Json::fromJson<ContentBlock>(obj);
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

QJsonObject PromptParams::toJson() const
{
    return Json::toJson(*this);
}

PromptParams PromptParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PromptParams>(obj);
}

QJsonObject PromptResult::toJson() const
{
    return Json::toJson(*this);
}

PromptResult PromptResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PromptResult>(obj);
}

QJsonObject ToolCallLocation::toJson() const
{
    return Json::toJson(*this);
}

ToolCallLocation ToolCallLocation::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ToolCallLocation>(obj);
}

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
    return Json::fromJson<ToolCallContent>(obj);
}

QJsonObject ToolCall::toJson() const
{
    return Json::toJson(*this);
}

ToolCall ToolCall::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ToolCall>(obj);
}

QJsonObject PlanEntry::toJson() const
{
    return Json::toJson(*this);
}

PlanEntry PlanEntry::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PlanEntry>(obj);
}

QJsonObject Plan::toJson() const
{
    return Json::toJson(*this);
}

Plan Plan::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<Plan>(obj);
}

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
    } else if (sessionUpdate == QLatin1String(SessionUpdateKind::ConfigOptionUpdate)) {
        o.insert("configOptions", configOptionsToJson(configOptions));
    } else if (sessionUpdate == QLatin1String(SessionUpdateKind::UsageUpdate)) {
        for (auto it = usage.constBegin(); it != usage.constEnd(); ++it)
            o.insert(it.key(), it.value());
    } else if (sessionUpdate == QLatin1String(SessionUpdateKind::SessionInfoUpdate)) {
        o.insert("title", title);
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
    else if (u.sessionUpdate == QLatin1String(SessionUpdateKind::ToolCall)
             || u.sessionUpdate == QLatin1String(SessionUpdateKind::ToolCallUpdate))
        u.toolCall = ToolCall::fromJson(obj);

    if (obj.contains("plan") && obj.value("plan").isObject())
        u.plan = Plan::fromJson(obj.value("plan").toObject());
    else if (u.sessionUpdate == QLatin1String(SessionUpdateKind::Plan))
        u.plan = Plan::fromJson(obj);
    for (const QJsonValue &v : obj.value("availableCommands").toArray())
        u.availableCommands.append(AvailableCommand::fromJson(v.toObject()));
    u.currentModeId = obj.value("currentModeId").toString();
    u.configOptions = configOptionsFromJson(obj.value("configOptions").toArray());
    if (u.sessionUpdate == QLatin1String(SessionUpdateKind::UsageUpdate)) {
        u.usage = obj;
        u.usage.remove(QStringLiteral("sessionUpdate"));
    }
    u.title = obj.value("title").toString();
    return u;
}

QJsonObject SessionNotification::toJson() const
{
    return Json::toJson(*this);
}

SessionNotification SessionNotification::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<SessionNotification>(obj);
}

QJsonObject PermissionOption::toJson() const
{
    return Json::toJson(*this);
}

PermissionOption PermissionOption::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PermissionOption>(obj);
}

QJsonObject RequestPermissionParams::toJson() const
{
    return Json::toJson(*this);
}

RequestPermissionParams RequestPermissionParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<RequestPermissionParams>(obj);
}

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

QJsonObject ReadTextFileParams::toJson() const
{
    return Json::toJson(*this);
}

ReadTextFileParams ReadTextFileParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ReadTextFileParams>(obj);
}

QJsonObject ReadTextFileResult::toJson() const
{
    return Json::toJson(*this);
}

ReadTextFileResult ReadTextFileResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ReadTextFileResult>(obj);
}

QJsonObject WriteTextFileParams::toJson() const
{
    return Json::toJson(*this);
}

WriteTextFileParams WriteTextFileParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<WriteTextFileParams>(obj);
}

QJsonObject CreateTerminalParams::toJson() const
{
    return Json::toJson(*this);
}

CreateTerminalParams CreateTerminalParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<CreateTerminalParams>(obj);
}

QJsonObject CreateTerminalResult::toJson() const
{
    return Json::toJson(*this);
}

CreateTerminalResult CreateTerminalResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<CreateTerminalResult>(obj);
}

QJsonObject ExitStatus::toJson() const
{
    return Json::toJson(*this);
}

ExitStatus ExitStatus::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ExitStatus>(obj);
}

QJsonObject TerminalOutputParams::toJson() const
{
    return Json::toJson(*this);
}

TerminalOutputParams TerminalOutputParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<TerminalOutputParams>(obj);
}

QJsonObject TerminalOutputResult::toJson() const
{
    return Json::toJson(*this);
}

TerminalOutputResult TerminalOutputResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<TerminalOutputResult>(obj);
}

QJsonObject TerminalRefParams::toJson() const
{
    return Json::toJson(*this);
}

TerminalRefParams TerminalRefParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<TerminalRefParams>(obj);
}

QJsonObject WaitForTerminalExitResult::toJson() const
{
    return Json::toJson(*this);
}

WaitForTerminalExitResult WaitForTerminalExitResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<WaitForTerminalExitResult>(obj);
}

} // namespace LLMQore::Acp
