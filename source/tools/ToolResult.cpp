// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/ToolResult.hpp>

#include <QJsonValue>
#include <QMetaType>
#include <QStringList>

namespace LLMCore {

namespace {
const int _toolResultMetaType = []() {
    qRegisterMetaType<LLMCore::ToolContent>("LLMCore::ToolContent");
    qRegisterMetaType<LLMCore::ToolResult>("LLMCore::ToolResult");
    qRegisterMetaType<LLMCoreToolResultHash>("LLMCoreToolResultHash");
    return 0;
}();
} // namespace

ToolContent ToolContent::makeText(const QString &text)
{
    ToolContent c;
    c.type = Text;
    c.text = text;
    return c;
}

ToolContent ToolContent::makeImage(const QByteArray &data, const QString &mimeType)
{
    ToolContent c;
    c.type = Image;
    c.data = data;
    c.mimeType = mimeType;
    return c;
}

ToolContent ToolContent::makeAudio(const QByteArray &data, const QString &mimeType)
{
    ToolContent c;
    c.type = Audio;
    c.data = data;
    c.mimeType = mimeType;
    return c;
}

ToolContent ToolContent::makeResourceText(
    const QString &uri, const QString &text, const QString &mimeType)
{
    ToolContent c;
    c.type = Resource;
    c.uri = uri;
    c.resourceText = text;
    c.mimeType = mimeType;
    return c;
}

ToolContent ToolContent::makeResourceBlob(
    const QString &uri, const QByteArray &blob, const QString &mimeType)
{
    ToolContent c;
    c.type = Resource;
    c.uri = uri;
    c.resourceBlob = blob;
    c.mimeType = mimeType;
    return c;
}

ToolContent ToolContent::makeResourceLink(
    const QString &uri,
    const QString &name,
    const QString &description,
    const QString &mimeType)
{
    ToolContent c;
    c.type = ResourceLink;
    c.uri = uri;
    c.name = name;
    c.description = description;
    c.mimeType = mimeType;
    return c;
}

QJsonObject ToolContent::toJson() const
{
    switch (type) {
    case Text:
        return QJsonObject{{"type", "text"}, {"text", text}};

    case Image: {
        QJsonObject obj{{"type", "image"}, {"data", QString::fromUtf8(data.toBase64())}};
        if (!mimeType.isEmpty())
            obj.insert("mimeType", mimeType);
        return obj;
    }
    case Audio: {
        QJsonObject obj{{"type", "audio"}, {"data", QString::fromUtf8(data.toBase64())}};
        if (!mimeType.isEmpty())
            obj.insert("mimeType", mimeType);
        return obj;
    }
    case Resource: {
        QJsonObject resource{{"uri", uri}};
        if (!resourceText.isEmpty())
            resource.insert("text", resourceText);
        else if (!resourceBlob.isEmpty())
            resource.insert("blob", QString::fromUtf8(resourceBlob.toBase64()));
        if (!mimeType.isEmpty())
            resource.insert("mimeType", mimeType);
        return QJsonObject{{"type", "resource"}, {"resource", resource}};
    }
    case ResourceLink: {
        QJsonObject obj{{"type", "resource_link"}, {"uri", uri}};
        if (!name.isEmpty())
            obj.insert("name", name);
        if (!description.isEmpty())
            obj.insert("description", description);
        if (!mimeType.isEmpty())
            obj.insert("mimeType", mimeType);
        return obj;
    }
    }
    return QJsonObject{{"type", "text"}, {"text", QString()}};
}

ToolContent ToolContent::fromJson(const QJsonObject &obj)
{
    ToolContent c;
    const QString type = obj.value("type").toString();

    if (type == QLatin1String("text")) {
        c.type = Text;
        c.text = obj.value("text").toString();
    } else if (type == QLatin1String("image")) {
        c.type = Image;
        c.data = QByteArray::fromBase64(obj.value("data").toString().toUtf8());
        c.mimeType = obj.value("mimeType").toString();
    } else if (type == QLatin1String("audio")) {
        c.type = Audio;
        c.data = QByteArray::fromBase64(obj.value("data").toString().toUtf8());
        c.mimeType = obj.value("mimeType").toString();
    } else if (type == QLatin1String("resource")) {
        c.type = Resource;
        const QJsonObject res = obj.value("resource").toObject();
        c.uri = res.value("uri").toString();
        c.mimeType = res.value("mimeType").toString();
        if (res.contains("text"))
            c.resourceText = res.value("text").toString();
        if (res.contains("blob"))
            c.resourceBlob = QByteArray::fromBase64(res.value("blob").toString().toUtf8());
    } else if (type == QLatin1String("resource_link")) {
        c.type = ResourceLink;
        c.uri = obj.value("uri").toString();
        c.name = obj.value("name").toString();
        c.description = obj.value("description").toString();
        c.mimeType = obj.value("mimeType").toString();
    } else {
        c.type = Text;
        c.text = QString("[unsupported content type: %1]").arg(type);
    }
    return c;
}

ToolResult ToolResult::text(const QString &text)
{
    ToolResult r;
    r.content.append(ToolContent::makeText(text));
    return r;
}

ToolResult ToolResult::error(const QString &message)
{
    ToolResult r;
    r.content.append(ToolContent::makeText(message));
    r.isError = true;
    return r;
}

ToolResult ToolResult::empty()
{
    return ToolResult{};
}

QString ToolResult::asText() const
{
    QStringList parts;
    parts.reserve(content.size());
    for (const ToolContent &block : content) {
        switch (block.type) {
        case ToolContent::Text:
            parts.append(block.text);
            break;
        case ToolContent::Image:
            parts.append(QString("[image: %1]").arg(
                block.mimeType.isEmpty() ? QStringLiteral("unknown") : block.mimeType));
            break;
        case ToolContent::Audio:
            parts.append(QString("[audio: %1]").arg(
                block.mimeType.isEmpty() ? QStringLiteral("unknown") : block.mimeType));
            break;
        case ToolContent::Resource:
            if (!block.resourceText.isEmpty())
                parts.append(block.resourceText);
            else
                parts.append(QString("[resource: %1]").arg(block.uri));
            break;
        case ToolContent::ResourceLink:
            parts.append(QString("[resource link: %1]").arg(block.uri));
            break;
        }
    }
    return parts.join(QLatin1Char('\n'));
}

bool ToolResult::isEmpty() const
{
    return content.isEmpty() && structuredContent.isEmpty();
}

QJsonObject ToolResult::toJson() const
{
    QJsonArray arr;
    for (const ToolContent &block : content)
        arr.append(block.toJson());

    QJsonObject obj{{"content", arr}};
    if (isError)
        obj.insert("isError", true);
    if (!structuredContent.isEmpty())
        obj.insert("structuredContent", structuredContent);
    return obj;
}

ToolResult ToolResult::fromJson(const QJsonObject &obj)
{
    ToolResult r;
    const QJsonArray arr = obj.value("content").toArray();
    for (const QJsonValue &v : arr)
        r.content.append(ToolContent::fromJson(v.toObject()));
    r.isError = obj.value("isError").toBool();
    r.structuredContent = obj.value("structuredContent").toObject();
    return r;
}

} // namespace LLMCore
