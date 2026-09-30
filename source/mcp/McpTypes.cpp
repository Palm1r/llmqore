// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/McpTypes.hpp>

#include "McpTypeSchema.hpp"

namespace LLMQore::Mcp {

QJsonObject IconInfo::toJson() const
{
    return Json::toJson(*this);
}

IconInfo IconInfo::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<IconInfo>(obj);
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
    return Json::toJson(*this);
}

Implementation Implementation::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<Implementation>(obj);
}

QJsonObject ToolsCapability::toJson() const
{
    return Json::toJson(*this);
}

ToolsCapability ToolsCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ToolsCapability>(obj);
}

QJsonObject ResourcesCapability::toJson() const
{
    return Json::toJson(*this);
}

ResourcesCapability ResourcesCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ResourcesCapability>(obj);
}

QJsonObject PromptsCapability::toJson() const
{
    return Json::toJson(*this);
}

PromptsCapability PromptsCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PromptsCapability>(obj);
}

QJsonObject LoggingCapability::toJson() const
{
    return Json::toJson(*this);
}

LoggingCapability LoggingCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<LoggingCapability>(obj);
}

QJsonObject CompletionsCapability::toJson() const
{
    return Json::toJson(*this);
}

CompletionsCapability CompletionsCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<CompletionsCapability>(obj);
}

QJsonObject ServerCapabilities::toJson() const
{
    return Json::toJson(*this);
}

ServerCapabilities ServerCapabilities::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ServerCapabilities>(obj);
}

QJsonObject RootsCapability::toJson() const
{
    return Json::toJson(*this);
}

RootsCapability RootsCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<RootsCapability>(obj);
}

QJsonObject SamplingCapability::toJson() const
{
    return Json::toJson(*this);
}

SamplingCapability SamplingCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<SamplingCapability>(obj);
}

QJsonObject ElicitationCapability::toJson() const
{
    return Json::toJson(*this);
}

ElicitationCapability ElicitationCapability::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ElicitationCapability>(obj);
}

QJsonObject ClientCapabilities::toJson() const
{
    return Json::toJson(*this);
}

ClientCapabilities ClientCapabilities::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ClientCapabilities>(obj);
}

QJsonObject InitializeResult::toJson() const
{
    return Json::toJson(*this);
}

InitializeResult InitializeResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<InitializeResult>(obj);
}

QJsonObject ToolInfo::toJson() const
{
    return Json::toJson(*this);
}

ToolInfo ToolInfo::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ToolInfo>(obj);
}

QJsonObject ResourceInfo::toJson() const
{
    return Json::toJson(*this);
}

ResourceInfo ResourceInfo::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ResourceInfo>(obj);
}

QJsonObject ResourceTemplate::toJson() const
{
    return Json::toJson(*this);
}

ResourceTemplate ResourceTemplate::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ResourceTemplate>(obj);
}

QJsonObject ResourceContents::toJson() const
{
    QJsonObject obj;
    obj.insert("uri", uri);
    if (!mimeType.isEmpty())
        obj.insert("mimeType", mimeType);
    if (text.isEmpty() && !blob.isEmpty())
        obj.insert("blob", QString::fromUtf8(blob.toBase64()));
    else
        obj.insert("text", text);
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
    return Json::toJson(*this);
}

PromptArgument PromptArgument::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PromptArgument>(obj);
}

QJsonObject PromptInfo::toJson() const
{
    return Json::toJson(*this);
}

PromptInfo PromptInfo::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PromptInfo>(obj);
}

QJsonObject PromptMessage::toJson() const
{
    return Json::toJson(*this);
}

PromptMessage PromptMessage::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PromptMessage>(obj);
}

QJsonObject PromptGetResult::toJson() const
{
    return Json::toJson(*this);
}

PromptGetResult PromptGetResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<PromptGetResult>(obj);
}

QJsonObject Root::toJson() const
{
    return Json::toJson(*this);
}

Root Root::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<Root>(obj);
}

QJsonObject CompletionReference::toJson() const
{
    return Json::toJson(*this);
}

CompletionReference CompletionReference::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<CompletionReference>(obj);
}

QJsonObject CompletionArgument::toJson() const
{
    return Json::toJson(*this);
}

CompletionArgument CompletionArgument::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<CompletionArgument>(obj);
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
    return Json::toJson(*this);
}

SamplingMessage SamplingMessage::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<SamplingMessage>(obj);
}

QJsonObject ModelHint::toJson() const
{
    return Json::toJson(*this);
}

ModelHint ModelHint::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ModelHint>(obj);
}

QJsonObject ModelPreferences::toJson() const
{
    return Json::toJson(*this);
}

ModelPreferences ModelPreferences::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ModelPreferences>(obj);
}

QJsonObject CreateMessageParams::toJson() const
{
    return Json::toJson(*this);
}

CreateMessageParams CreateMessageParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<CreateMessageParams>(obj);
}

QJsonObject CreateMessageResult::toJson() const
{
    QJsonObject obj = Json::toJson(*this);
    if (role.isEmpty())
        obj.insert("role", QStringLiteral("assistant"));
    return obj;
}

CreateMessageResult CreateMessageResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<CreateMessageResult>(obj);
}

QJsonObject ElicitRequestParams::toJson() const
{
    return Json::toJson(*this);
}

ElicitRequestParams ElicitRequestParams::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ElicitRequestParams>(obj);
}

QJsonObject ElicitResult::toJson() const
{
    QJsonObject obj{{"action", action}};
    if (action == QLatin1String(ElicitAction::Accept) && !content.isEmpty())
        obj.insert("content", content);
    return obj;
}

ElicitResult ElicitResult::fromJson(const QJsonObject &obj)
{
    return Json::fromJson<ElicitResult>(obj);
}

} // namespace LLMQore::Mcp
