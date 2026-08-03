// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include "OpenAIMessage.hpp"

#include <LLMQore/Log.hpp>

#include <QJsonArray>
#include <QJsonDocument>

namespace LLMQore {

namespace {

class OpenAIToolDialect : public ToolDialect
{
public:
    QJsonObject wrapDefinition(const BaseTool &tool) const override
    {
        return QJsonObject{
            {"type", "function"},
            {"function",
             QJsonObject{
                 {"name", tool.id()},
                 {"description", tool.description()},
                 {"parameters", tool.parametersSchema()}}}};
    }
};

} // namespace

const ToolDialect &OpenAIMessage::toolDialect()
{
    static const OpenAIToolDialect dialect;
    return dialect;
}


OpenAIMessage::ContentParts OpenAIMessage::splitContentParts(const QJsonValue &content)
{
    ContentParts out;

    if (content.isString()) {
        out.text = content.toString();
        return out;
    }
    if (!content.isArray())
        return out;

    const QJsonArray parts = content.toArray();
    for (const auto &partVal : parts) {
        const QJsonObject part = partVal.toObject();
        const QString type = part.value("type").toString();
        if (type == QLatin1String("text")) {
            out.text += part.value("text").toString();
        } else if (type == QLatin1String("thinking")) {
            const QJsonValue th = part.value("thinking");
            if (th.isString()) {
                out.thinking += th.toString();
            } else if (th.isArray()) {
                const QJsonArray thArr = th.toArray();
                for (const auto &tv : thArr) {
                    if (tv.isString()) {
                        out.thinking += tv.toString();
                    } else {
                        const QJsonObject thObj = tv.toObject();
                        if (thObj.value("type").toString() == QLatin1String("text"))
                            out.thinking += thObj.value("text").toString();
                    }
                }
            } else {
                out.thinking += part.value("text").toString();
            }
        }
    }

    return out;
}

OpenAIMessage::OpenAIMessage(QObject *parent)
    : BaseMessage(parent)
{}

void OpenAIMessage::handleContentDelta(const QString &content)
{
    appendTextDelta(content);
}

void OpenAIMessage::handleReasoningDelta(const QString &reasoning)
{
    const int index = getOrCreateThinkingContentIndex();
    if (auto *thinkingContent = blockAt<ThinkingContent>(index))
        thinkingContent->thinking += reasoning;
}

void OpenAIMessage::handleToolCallStart(int index, const QString &id, const QString &name)
{
    qCDebug(llmOpenAILog).noquote()
        << QString("handleToolCallStart index=%1, id=%2, name=%3").arg(index).arg(id, name);

    m_toolCalls.start(index, addCurrentContent(ToolUseContent{id, name, {}}));
}

void OpenAIMessage::handleToolCallDelta(int index, const QString &argumentsDelta)
{
    m_toolCalls.delta(index, argumentsDelta);
}

void OpenAIMessage::handleToolCallComplete(int index)
{
    completeToolCall(m_toolCalls, index);
}

void OpenAIMessage::completeAllPendingToolCalls()
{
    completeAllToolCalls(m_toolCalls);
}

void OpenAIMessage::handleStopReason(const QString &finishReason)
{
    static const StopReasonMap kMap{
        {QStringLiteral("tool_calls")},
        {},
        {QStringLiteral("stop")},
        {},
        MessageState::Complete,
        false};

    m_finishReason = finishReason;
    m_state = resolveState(m_finishReason, kMap);
}

QJsonObject OpenAIMessage::serializeTurn(TurnRole role, const QList<TurnContent> &blocks)
{
    const bool isAssistant = role == TurnRole::Assistant;

    QString textContent;
    QString reasoningContent;
    QJsonArray parts;
    QJsonArray toolCalls;

    for (const TurnContent &block : blocks) {
        std::visit(
            detail::overloaded{
                [&](const TextContent &c) {
                    textContent += c.text;
                    if (!isAssistant)
                        parts.append(QJsonObject{{"type", "text"}, {"text", c.text}});
                },
                [&](const ImageContent &c) {
                    if (isAssistant)
                        return;
                    const QString url = c.isUrl()
                        ? c.url().toString()
                        : QStringLiteral("data:%1;base64,%2")
                              .arg(
                                  c.mimeType.isEmpty() ? QStringLiteral("image/png") : c.mimeType,
                                  c.base64());
                    parts.append(
                        QJsonObject{
                            {"type", "image_url"}, {"image_url", QJsonObject{{"url", url}}}});
                },
                [&](const AudioContent &) {},
                [&](const ToolUseContent &c) {
                    const QJsonDocument doc(c.input);
                    toolCalls.append(
                        QJsonObject{
                            {"id", c.id},
                            {"type", "function"},
                            {"function",
                             QJsonObject{
                                 {"name", c.name},
                                 {"arguments",
                                  QString::fromUtf8(doc.toJson(QJsonDocument::Compact))}}}});
                },
                [&](const ToolResultContent &) {},
                [&](const ThinkingContent &c) { reasoningContent += c.thinking; },
                [&](const RedactedThinkingContent &) {}},
            block);
    }

    QJsonObject message;
    message["role"] = isAssistant ? QStringLiteral("assistant") : QStringLiteral("user");

    const bool hasNonText = parts.size() > 1
        || (parts.size() == 1 && parts.first().toObject().value("type").toString() != "text");

    if (!isAssistant && hasNonText)
        message["content"] = parts;
    else if (!textContent.isEmpty())
        message["content"] = textContent;
    else if (isAssistant)
        message["content"] = QJsonValue();

    if (!reasoningContent.isEmpty())
        message["reasoning_content"] = reasoningContent;
    if (!toolCalls.isEmpty())
        message["tool_calls"] = toolCalls;

    return message;
}

QJsonObject OpenAIMessage::toProviderFormat() const
{
    return serializeTurn(TurnRole::Assistant, m_currentBlocks);
}

QJsonArray OpenAIMessage::createToolResultMessages(
    const QHash<QString, ToolResult> &toolResults) const
{
    return mapToolResults(
        toolResults, [](const ToolUseContent &use, const ToolResult &r, QJsonArray &out) {
            out.append(
                QJsonObject{
                    {"role", "tool"},
                    {"tool_call_id", use.id},
                    {"content", toolResultText(r)}});
        });
}

void OpenAIMessage::clearDerivedCaches()
{
    m_toolCalls.clear();
    m_finishReason.clear();
}

} // namespace LLMQore
