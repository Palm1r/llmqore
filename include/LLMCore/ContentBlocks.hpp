// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace LLMCore {

enum class MessageState { Building, Complete, RequiresToolExecution, Final };

class ContentBlock
{
public:
    ContentBlock() = default;
    virtual ~ContentBlock() = default;

    ContentBlock(const ContentBlock &) = delete;
    ContentBlock &operator=(const ContentBlock &) = delete;

    virtual QString type() const = 0;
};

class TextContent : public ContentBlock
{
public:
    explicit TextContent(const QString &text = QString())
        : m_text(text)
    {}

    QString type() const override { return "text"; }
    QString text() const { return m_text; }
    void appendText(const QString &text) { m_text += text; }
    void setText(const QString &text) { m_text = text; }

private:
    QString m_text;
};

class ImageContent : public ContentBlock
{
public:
    enum class ImageSourceType { Base64, Url };

    ImageContent(
        const QString &data,
        const QString &mediaType,
        ImageSourceType sourceType = ImageSourceType::Base64)
        : m_data(data)
        , m_mediaType(mediaType)
        , m_sourceType(sourceType)
    {}

    QString type() const override { return "image"; }
    QString data() const { return m_data; }
    QString mediaType() const { return m_mediaType; }
    ImageSourceType sourceType() const { return m_sourceType; }

private:
    QString m_data;
    QString m_mediaType;
    ImageSourceType m_sourceType;
};

class ToolUseContent : public ContentBlock
{
public:
    ToolUseContent(const QString &id, const QString &name, const QJsonObject &input = QJsonObject())
        : m_id(id)
        , m_name(name)
        , m_input(input)
    {}

    QString type() const override { return "tool_use"; }
    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QJsonObject input() const { return m_input; }
    void setInput(const QJsonObject &input) { m_input = input; }

private:
    QString m_id;
    QString m_name;
    QJsonObject m_input;
};

class ToolResultContent : public ContentBlock
{
public:
    ToolResultContent(const QString &toolUseId, const QString &result)
        : m_toolUseId(toolUseId)
        , m_result(result)
    {}

    QString type() const override { return "tool_result"; }
    QString toolUseId() const { return m_toolUseId; }
    QString result() const { return m_result; }

private:
    QString m_toolUseId;
    QString m_result;
};

class ThinkingContent : public ContentBlock
{
public:
    explicit ThinkingContent(
        const QString &thinking = QString(), const QString &signature = QString())
        : m_thinking(thinking)
        , m_signature(signature)
    {}

    QString type() const override { return "thinking"; }
    QString thinking() const { return m_thinking; }
    QString signature() const { return m_signature; }
    void appendThinking(const QString &text) { m_thinking += text; }
    void setThinking(const QString &text) { m_thinking = text; }
    void setSignature(const QString &signature) { m_signature = signature; }

private:
    QString m_thinking;
    QString m_signature;
};

class RedactedThinkingContent : public ContentBlock
{
public:
    explicit RedactedThinkingContent(const QString &signature = QString())
        : m_signature(signature)
    {}

    QString type() const override { return "redacted_thinking"; }
    QString signature() const { return m_signature; }
    void setSignature(const QString &signature) { m_signature = signature; }

private:
    QString m_signature;
};

// Ownership: caller (typically BaseMessage) takes ownership of the returned pointer.
// All blocks added to a BaseMessage are deleted in ~BaseMessage and startNewContinuation.
template<typename T, typename... Args>
T *addContentBlock(QList<ContentBlock *> &blocks, Args &&...args)
{
    T *content = new T(std::forward<Args>(args)...);
    blocks.append(content);
    return content;
}

} // namespace LLMCore
