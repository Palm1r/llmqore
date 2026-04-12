// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpTypes.hpp>

namespace LLMCore::Mcp {

namespace {

QJsonObject stripKnown(const QJsonObject &obj, std::initializer_list<const char *> keys)
{
    QJsonObject out = obj;
    for (const char *k : keys)
        out.remove(QLatin1String(k));
    return out;
}

QJsonObject takeMeta(const QJsonObject &obj)
{
    return obj.value("_meta").toObject();
}

void insertMetaIfAny(QJsonObject &out, const QJsonObject &meta)
{
    if (!meta.isEmpty())
        out.insert("_meta", meta);
}

} // namespace

QJsonObject IconInfo::toJson() const
{
    QJsonObject obj;
    obj.insert("src", src);
    if (!mimeType.isEmpty())
        obj.insert("mimeType", mimeType);
    if (!sizes.isEmpty())
        obj.insert("sizes", sizes);
    return obj;
}

IconInfo IconInfo::fromJson(const QJsonObject &obj)
{
    IconInfo info;
    info.src = obj.value("src").toString();
    info.mimeType = obj.value("mimeType").toString();
    info.sizes = obj.value("sizes").toString();
    return info;
}

QJsonArray iconsToJson(const QList<IconInfo> &icons)
{
    QJsonArray arr;
    for (const IconInfo &i : icons)
        arr.append(i.toJson());
    return arr;
}

QList<IconInfo> iconsFromJson(const QJsonArray &arr)
{
    QList<IconInfo> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.append(IconInfo::fromJson(v.toObject()));
    return out;
}

QJsonObject Implementation::toJson() const
{
    QJsonObject obj{{"name", name}, {"version", version}};
    if (!description.isEmpty())
        obj.insert("description", description);
    if (!title.isEmpty())
        obj.insert("title", title);
    if (!icons.isEmpty())
        obj.insert("icons", iconsToJson(icons));
    return obj;
}

Implementation Implementation::fromJson(const QJsonObject &obj)
{
    Implementation info;
    info.name = obj.value("name").toString();
    info.version = obj.value("version").toString();
    info.description = obj.value("description").toString();
    info.title = obj.value("title").toString();
    if (obj.contains("icons"))
        info.icons = iconsFromJson(obj.value("icons").toArray());
    return info;
}

QJsonObject ToolsCapability::toJson() const
{
    QJsonObject obj;
    if (listChanged)
        obj.insert("listChanged", true);
    return obj;
}

ToolsCapability ToolsCapability::fromJson(const QJsonObject &obj)
{
    ToolsCapability cap;
    cap.listChanged = obj.value("listChanged").toBool();
    return cap;
}

QJsonObject ResourcesCapability::toJson() const
{
    QJsonObject obj;
    if (subscribe)
        obj.insert("subscribe", true);
    if (listChanged)
        obj.insert("listChanged", true);
    return obj;
}

ResourcesCapability ResourcesCapability::fromJson(const QJsonObject &obj)
{
    ResourcesCapability cap;
    cap.subscribe = obj.value("subscribe").toBool();
    cap.listChanged = obj.value("listChanged").toBool();
    return cap;
}

QJsonObject PromptsCapability::toJson() const
{
    QJsonObject obj;
    if (listChanged)
        obj.insert("listChanged", true);
    return obj;
}

PromptsCapability PromptsCapability::fromJson(const QJsonObject &obj)
{
    PromptsCapability cap;
    cap.listChanged = obj.value("listChanged").toBool();
    return cap;
}

QJsonObject LoggingCapability::toJson() const
{
    return QJsonObject{};
}

LoggingCapability LoggingCapability::fromJson(const QJsonObject &)
{
    return LoggingCapability{true};
}

QJsonObject CompletionsCapability::toJson() const
{
    return QJsonObject{};
}

CompletionsCapability CompletionsCapability::fromJson(const QJsonObject &)
{
    return CompletionsCapability{true};
}

QJsonObject ServerCapabilities::toJson() const
{
    QJsonObject obj = extras;
    if (tools.has_value())
        obj.insert("tools", tools->toJson());
    if (resources.has_value())
        obj.insert("resources", resources->toJson());
    if (prompts.has_value())
        obj.insert("prompts", prompts->toJson());
    if (logging.has_value())
        obj.insert("logging", logging->toJson());
    if (completions.has_value())
        obj.insert("completions", completions->toJson());
    return obj;
}

ServerCapabilities ServerCapabilities::fromJson(const QJsonObject &obj)
{
    ServerCapabilities caps;
    if (obj.contains("tools"))
        caps.tools = ToolsCapability::fromJson(obj.value("tools").toObject());
    if (obj.contains("resources"))
        caps.resources = ResourcesCapability::fromJson(obj.value("resources").toObject());
    if (obj.contains("prompts"))
        caps.prompts = PromptsCapability::fromJson(obj.value("prompts").toObject());
    if (obj.contains("logging"))
        caps.logging = LoggingCapability::fromJson(obj.value("logging").toObject());
    if (obj.contains("completions"))
        caps.completions = CompletionsCapability::fromJson(obj.value("completions").toObject());

    caps.extras = stripKnown(obj, {"tools", "resources", "prompts", "logging", "completions"});
    return caps;
}

QJsonObject RootsCapability::toJson() const
{
    QJsonObject obj;
    if (listChanged)
        obj.insert("listChanged", true);
    return obj;
}

RootsCapability RootsCapability::fromJson(const QJsonObject &obj)
{
    RootsCapability cap;
    cap.listChanged = obj.value("listChanged").toBool();
    return cap;
}

QJsonObject SamplingCapability::toJson() const
{
    return QJsonObject{};
}

SamplingCapability SamplingCapability::fromJson(const QJsonObject &)
{
    return SamplingCapability{true};
}

QJsonObject ElicitationCapability::toJson() const
{
    return QJsonObject{};
}

ElicitationCapability ElicitationCapability::fromJson(const QJsonObject &)
{
    return ElicitationCapability{true};
}

QJsonObject ClientCapabilities::toJson() const
{
    QJsonObject obj = extras;
    if (roots.has_value())
        obj.insert("roots", roots->toJson());
    if (sampling.has_value())
        obj.insert("sampling", sampling->toJson());
    if (elicitation.has_value())
        obj.insert("elicitation", elicitation->toJson());
    return obj;
}

ClientCapabilities ClientCapabilities::fromJson(const QJsonObject &obj)
{
    ClientCapabilities caps;
    if (obj.contains("roots"))
        caps.roots = RootsCapability::fromJson(obj.value("roots").toObject());
    if (obj.contains("sampling"))
        caps.sampling = SamplingCapability::fromJson(obj.value("sampling").toObject());
    if (obj.contains("elicitation"))
        caps.elicitation = ElicitationCapability::fromJson(obj.value("elicitation").toObject());
    caps.extras = stripKnown(obj, {"roots", "sampling", "elicitation"});
    return caps;
}

QJsonObject InitializeResult::toJson() const
{
    QJsonObject obj;
    obj.insert("protocolVersion", protocolVersion);
    obj.insert("capabilities", capabilities.toJson());
    obj.insert("serverInfo", serverInfo.toJson());
    if (!instructions.isEmpty())
        obj.insert("instructions", instructions);
    return obj;
}

InitializeResult InitializeResult::fromJson(const QJsonObject &obj)
{
    InitializeResult result;
    result.protocolVersion = obj.value("protocolVersion").toString();
    result.capabilities = ServerCapabilities::fromJson(obj.value("capabilities").toObject());
    result.serverInfo = Implementation::fromJson(obj.value("serverInfo").toObject());
    result.instructions = obj.value("instructions").toString();
    return result;
}

QJsonObject ToolInfo::toJson() const
{
    QJsonObject obj;
    obj.insert("name", name);
    if (!title.isEmpty())
        obj.insert("title", title);
    if (!description.isEmpty())
        obj.insert("description", description);
    obj.insert("inputSchema", inputSchema);
    if (!outputSchema.isEmpty())
        obj.insert("outputSchema", outputSchema);
    if (!annotations.isEmpty())
        obj.insert("annotations", annotations);
    if (!icons.isEmpty())
        obj.insert("icons", iconsToJson(icons));
    insertMetaIfAny(obj, meta);
    return obj;
}

ToolInfo ToolInfo::fromJson(const QJsonObject &obj)
{
    ToolInfo info;
    info.name = obj.value("name").toString();
    info.title = obj.value("title").toString();
    info.description = obj.value("description").toString();
    info.inputSchema = obj.value("inputSchema").toObject();
    info.outputSchema = obj.value("outputSchema").toObject();
    info.annotations = obj.value("annotations").toObject();
    if (obj.contains("icons"))
        info.icons = iconsFromJson(obj.value("icons").toArray());
    info.meta = takeMeta(obj);
    return info;
}

QJsonObject ResourceInfo::toJson() const
{
    QJsonObject obj;
    obj.insert("uri", uri);
    if (!name.isEmpty())
        obj.insert("name", name);
    if (!title.isEmpty())
        obj.insert("title", title);
    if (!description.isEmpty())
        obj.insert("description", description);
    if (!mimeType.isEmpty())
        obj.insert("mimeType", mimeType);
    if (!icons.isEmpty())
        obj.insert("icons", iconsToJson(icons));
    insertMetaIfAny(obj, meta);
    return obj;
}

ResourceInfo ResourceInfo::fromJson(const QJsonObject &obj)
{
    ResourceInfo info;
    info.uri = obj.value("uri").toString();
    info.name = obj.value("name").toString();
    info.title = obj.value("title").toString();
    info.description = obj.value("description").toString();
    info.mimeType = obj.value("mimeType").toString();
    if (obj.contains("icons"))
        info.icons = iconsFromJson(obj.value("icons").toArray());
    info.meta = takeMeta(obj);
    return info;
}

QJsonObject ResourceTemplate::toJson() const
{
    QJsonObject obj;
    obj.insert("uriTemplate", uriTemplate);
    if (!name.isEmpty())
        obj.insert("name", name);
    if (!title.isEmpty())
        obj.insert("title", title);
    if (!description.isEmpty())
        obj.insert("description", description);
    if (!mimeType.isEmpty())
        obj.insert("mimeType", mimeType);
    if (!icons.isEmpty())
        obj.insert("icons", iconsToJson(icons));
    insertMetaIfAny(obj, meta);
    return obj;
}

ResourceTemplate ResourceTemplate::fromJson(const QJsonObject &obj)
{
    ResourceTemplate tpl;
    tpl.uriTemplate = obj.value("uriTemplate").toString();
    tpl.name = obj.value("name").toString();
    tpl.title = obj.value("title").toString();
    tpl.description = obj.value("description").toString();
    tpl.mimeType = obj.value("mimeType").toString();
    if (obj.contains("icons"))
        tpl.icons = iconsFromJson(obj.value("icons").toArray());
    tpl.meta = takeMeta(obj);
    return tpl;
}

QJsonObject ResourceContents::toJson() const
{
    QJsonObject obj;
    obj.insert("uri", uri);
    if (!mimeType.isEmpty())
        obj.insert("mimeType", mimeType);
    if (!text.isEmpty()) {
        obj.insert("text", text);
    } else if (!blob.isEmpty()) {
        obj.insert("blob", QString::fromUtf8(blob.toBase64()));
    }
    return obj;
}

ResourceContents ResourceContents::fromJson(const QJsonObject &obj)
{
    ResourceContents contents;
    contents.uri = obj.value("uri").toString();
    contents.mimeType = obj.value("mimeType").toString();
    contents.text = obj.value("text").toString();
    if (obj.contains("blob")) {
        const QString b64 = obj.value("blob").toString();
        contents.blob = QByteArray::fromBase64(b64.toUtf8());
    }
    return contents;
}

QJsonObject PromptArgument::toJson() const
{
    QJsonObject obj;
    obj.insert("name", name);
    if (!description.isEmpty())
        obj.insert("description", description);
    if (required)
        obj.insert("required", true);
    return obj;
}

PromptArgument PromptArgument::fromJson(const QJsonObject &obj)
{
    PromptArgument a;
    a.name = obj.value("name").toString();
    a.description = obj.value("description").toString();
    a.required = obj.value("required").toBool();
    return a;
}

QJsonObject PromptInfo::toJson() const
{
    QJsonObject obj;
    obj.insert("name", name);
    if (!title.isEmpty())
        obj.insert("title", title);
    if (!description.isEmpty())
        obj.insert("description", description);
    if (!arguments.isEmpty()) {
        QJsonArray arr;
        for (const PromptArgument &a : arguments)
            arr.append(a.toJson());
        obj.insert("arguments", arr);
    }
    if (!icons.isEmpty())
        obj.insert("icons", iconsToJson(icons));
    insertMetaIfAny(obj, meta);
    return obj;
}

PromptInfo PromptInfo::fromJson(const QJsonObject &obj)
{
    PromptInfo info;
    info.name = obj.value("name").toString();
    info.title = obj.value("title").toString();
    info.description = obj.value("description").toString();
    const QJsonArray arr = obj.value("arguments").toArray();
    for (const QJsonValue &v : arr)
        info.arguments.append(PromptArgument::fromJson(v.toObject()));
    if (obj.contains("icons"))
        info.icons = iconsFromJson(obj.value("icons").toArray());
    info.meta = takeMeta(obj);
    return info;
}

QJsonObject PromptMessage::toJson() const
{
    return QJsonObject{{"role", role}, {"content", content}};
}

PromptMessage PromptMessage::fromJson(const QJsonObject &obj)
{
    PromptMessage m;
    m.role = obj.value("role").toString();
    m.content = obj.value("content").toObject();
    return m;
}

QJsonObject PromptGetResult::toJson() const
{
    QJsonObject obj;
    if (!description.isEmpty())
        obj.insert("description", description);
    QJsonArray arr;
    for (const PromptMessage &m : messages)
        arr.append(m.toJson());
    obj.insert("messages", arr);
    return obj;
}

PromptGetResult PromptGetResult::fromJson(const QJsonObject &obj)
{
    PromptGetResult r;
    r.description = obj.value("description").toString();
    const QJsonArray arr = obj.value("messages").toArray();
    for (const QJsonValue &v : arr)
        r.messages.append(PromptMessage::fromJson(v.toObject()));
    return r;
}

QJsonObject Root::toJson() const
{
    QJsonObject obj{{"uri", uri}};
    if (!name.isEmpty())
        obj.insert("name", name);
    return obj;
}

Root Root::fromJson(const QJsonObject &obj)
{
    Root r;
    r.uri = obj.value("uri").toString();
    r.name = obj.value("name").toString();
    return r;
}

QJsonObject CompletionReference::toJson() const
{
    QJsonObject obj{{"type", type}};
    if (!name.isEmpty())
        obj.insert("name", name);
    if (!uri.isEmpty())
        obj.insert("uri", uri);
    return obj;
}

CompletionReference CompletionReference::fromJson(const QJsonObject &obj)
{
    CompletionReference ref;
    ref.type = obj.value("type").toString();
    ref.name = obj.value("name").toString();
    ref.uri = obj.value("uri").toString();
    return ref;
}

QJsonObject CompletionArgument::toJson() const
{
    return QJsonObject{{"name", name}, {"value", value}};
}

CompletionArgument CompletionArgument::fromJson(const QJsonObject &obj)
{
    CompletionArgument arg;
    arg.name = obj.value("name").toString();
    arg.value = obj.value("value").toString();
    return arg;
}

QJsonObject CompletionResult::toJson() const
{
    QJsonArray vals;
    for (const QString &v : values)
        vals.append(v);
    QJsonObject completion{{"values", vals}};
    if (total.has_value())
        completion.insert("total", *total);
    if (hasMore)
        completion.insert("hasMore", true);
    return QJsonObject{{"completion", completion}};
}

CompletionResult CompletionResult::fromJson(const QJsonObject &obj)
{
    CompletionResult r;
    const QJsonObject completion = obj.value("completion").toObject();
    const QJsonArray arr = completion.value("values").toArray();
    for (const QJsonValue &v : arr)
        r.values.append(v.toString());
    if (completion.contains("total"))
        r.total = completion.value("total").toInt();
    r.hasMore = completion.value("hasMore").toBool();
    return r;
}

QJsonObject SamplingMessage::toJson() const
{
    return QJsonObject{{"role", role}, {"content", content}};
}

SamplingMessage SamplingMessage::fromJson(const QJsonObject &obj)
{
    SamplingMessage m;
    m.role = obj.value("role").toString();
    m.content = obj.value("content").toObject();
    return m;
}

QJsonObject ModelHint::toJson() const
{
    QJsonObject obj;
    if (!name.isEmpty())
        obj.insert("name", name);
    return obj;
}

ModelHint ModelHint::fromJson(const QJsonObject &obj)
{
    ModelHint h;
    h.name = obj.value("name").toString();
    return h;
}

QJsonObject ModelPreferences::toJson() const
{
    QJsonObject obj;
    if (!hints.isEmpty()) {
        QJsonArray arr;
        for (const ModelHint &h : hints)
            arr.append(h.toJson());
        obj.insert("hints", arr);
    }
    if (costPriority.has_value())
        obj.insert("costPriority", *costPriority);
    if (speedPriority.has_value())
        obj.insert("speedPriority", *speedPriority);
    if (intelligencePriority.has_value())
        obj.insert("intelligencePriority", *intelligencePriority);
    return obj;
}

ModelPreferences ModelPreferences::fromJson(const QJsonObject &obj)
{
    ModelPreferences p;
    const QJsonArray arr = obj.value("hints").toArray();
    for (const QJsonValue &v : arr)
        p.hints.append(ModelHint::fromJson(v.toObject()));
    if (obj.contains("costPriority"))
        p.costPriority = obj.value("costPriority").toDouble();
    if (obj.contains("speedPriority"))
        p.speedPriority = obj.value("speedPriority").toDouble();
    if (obj.contains("intelligencePriority"))
        p.intelligencePriority = obj.value("intelligencePriority").toDouble();
    return p;
}

QJsonObject CreateMessageParams::toJson() const
{
    QJsonObject obj;
    QJsonArray msgs;
    for (const SamplingMessage &m : messages)
        msgs.append(m.toJson());
    obj.insert("messages", msgs);
    if (modelPreferences.has_value())
        obj.insert("modelPreferences", modelPreferences->toJson());
    if (!systemPrompt.isEmpty())
        obj.insert("systemPrompt", systemPrompt);
    if (!includeContext.isEmpty())
        obj.insert("includeContext", includeContext);
    if (temperature.has_value())
        obj.insert("temperature", *temperature);
    obj.insert("maxTokens", maxTokens);
    if (!stopSequences.isEmpty()) {
        QJsonArray arr;
        for (const QString &s : stopSequences)
            arr.append(s);
        obj.insert("stopSequences", arr);
    }
    if (!metadata.isEmpty())
        obj.insert("metadata", metadata);
    return obj;
}

CreateMessageParams CreateMessageParams::fromJson(const QJsonObject &obj)
{
    CreateMessageParams p;
    const QJsonArray msgs = obj.value("messages").toArray();
    for (const QJsonValue &v : msgs)
        p.messages.append(SamplingMessage::fromJson(v.toObject()));
    if (obj.contains("modelPreferences"))
        p.modelPreferences = ModelPreferences::fromJson(obj.value("modelPreferences").toObject());
    p.systemPrompt = obj.value("systemPrompt").toString();
    p.includeContext = obj.value("includeContext").toString();
    if (obj.contains("temperature"))
        p.temperature = obj.value("temperature").toDouble();
    p.maxTokens = obj.value("maxTokens").toInt();
    const QJsonArray stops = obj.value("stopSequences").toArray();
    for (const QJsonValue &v : stops)
        p.stopSequences.append(v.toString());
    p.metadata = obj.value("metadata").toObject();
    return p;
}

QJsonObject CreateMessageResult::toJson() const
{
    QJsonObject obj;
    obj.insert("role", role.isEmpty() ? QStringLiteral("assistant") : role);
    obj.insert("content", content);
    if (!model.isEmpty())
        obj.insert("model", model);
    if (!stopReason.isEmpty())
        obj.insert("stopReason", stopReason);
    return obj;
}

CreateMessageResult CreateMessageResult::fromJson(const QJsonObject &obj)
{
    CreateMessageResult r;
    r.role = obj.value("role").toString();
    r.content = obj.value("content").toObject();
    r.model = obj.value("model").toString();
    r.stopReason = obj.value("stopReason").toString();
    return r;
}

QJsonObject ElicitRequestParams::toJson() const
{
    QJsonObject obj;
    obj.insert("message", message);
    if (!requestedSchema.isEmpty())
        obj.insert("requestedSchema", requestedSchema);
    if (!mode.isEmpty())
        obj.insert("mode", mode);
    if (!url.isEmpty())
        obj.insert("url", url);
    return obj;
}

ElicitRequestParams ElicitRequestParams::fromJson(const QJsonObject &obj)
{
    ElicitRequestParams p;
    p.message = obj.value("message").toString();
    p.requestedSchema = obj.value("requestedSchema").toObject();
    p.mode = obj.value("mode").toString();
    p.url = obj.value("url").toString();
    return p;
}

QJsonObject ElicitResult::toJson() const
{
    QJsonObject obj;
    obj.insert("action", action);
    if (action == QLatin1String(ElicitAction::Accept) && !content.isEmpty())
        obj.insert("content", content);
    return obj;
}

ElicitResult ElicitResult::fromJson(const QJsonObject &obj)
{
    ElicitResult r;
    r.action = obj.value("action").toString();
    r.content = obj.value("content").toObject();
    return r;
}

} // namespace LLMCore::Mcp
