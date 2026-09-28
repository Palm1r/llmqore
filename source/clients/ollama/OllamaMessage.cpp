// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include "OllamaMessage.hpp"
#include <LLMQore/Log.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include "core/ErrorEnvelope.hpp"

namespace LLMQore {

namespace {

class OllamaToolDialect : public ToolDialect
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

const ToolDialect &OllamaMessage::toolDialect()
{
    static const OllamaToolDialect dialect;
    return dialect;
}


OllamaMessage::OllamaMessage(QObject *parent)
    : BaseMessage(parent)
{}

MessageEffects OllamaMessage::applyEvent(const QJsonObject &event)
{
    MessageEffects effects;

    if (const std::optional<QJsonObject> error = errorIn(event)) {
        effects.error = *error;
        return effects;
    }

    const QString thinking = event.value("thinking").toString();
    if (!thinking.isEmpty())
        handleThinkingDelta(thinking);

    if (event.contains("message")) {
        const QJsonObject message = event.value("message").toObject();

        const QString messageThinking = message.value("thinking").toString();
        if (!messageThinking.isEmpty())
            handleThinkingDelta(messageThinking);

        const QString content = message.value("content").toString();
        if (!content.isEmpty()) {
            effects.thinkingCompleted = true;
            handleContentDelta(content);
            if (!isAccumulatingToolCall())
                effects.chunk = content;
        }

        const QJsonArray toolCalls = message.value("tool_calls").toArray();
        for (const QJsonValue &toolCall : toolCalls)
            handleToolCall(toolCall.toObject());

    } else if (event.contains("response")) {
        const QString content = event.value("response").toString();
        if (!content.isEmpty()) {
            handleContentDelta(content);
            effects.chunk = content;
        }
    }

    if (event.value("done").toBool()) {
        if (event.contains("signature"))
            handleThinkingComplete(event.value("signature").toString());

        handleStopReason(event.value("done_reason").toString());

        effects.usage = event;
        effects.thinkingCompleted = true;
        effects.toolsReady = true;
    }

    return effects;
}

MessageEffects OllamaMessage::applyResponse(const QJsonObject &response)
{
    return applyEvent(response);
}

void OllamaMessage::handleContentDelta(const QString &content)
{
    m_accumulatedContent += content;
    QString trimmed = m_accumulatedContent.trimmed();

    if (trimmed.startsWith('{') || trimmed.startsWith('`')) {
        return;
    }

    if (!m_contentAddedToTextBlock) {
        if (auto *textContent = blockAt<TextContent>(ensureTextContentIndex()))
            textContent->text = m_accumulatedContent;
        m_contentAddedToTextBlock = true;
        qCDebug(llmOllamaLog).noquote()
            << QString("Added accumulated content to TextContent, length=%1")
                   .arg(m_accumulatedContent.length());
    } else {
        appendTextDelta(content);
    }
}

void OllamaMessage::handleToolCall(const QJsonObject &toolCall)
{
    QJsonObject function = toolCall["function"].toObject();
    QString name = function["name"].toString();
    QJsonObject arguments = function["arguments"].toObject();

    QString toolId = makeToolCallId(name);

    if (!m_contentAddedToTextBlock && !m_accumulatedContent.trimmed().isEmpty()) {
        qCDebug(llmOllamaLog).noquote()
            << QString("Clearing accumulated content (tool call detected), length=%1")
                   .arg(m_accumulatedContent.length());
        m_accumulatedContent.clear();
    }

    addCurrentContent(ToolUseContent{toolId, name, arguments});

    qCDebug(llmOllamaLog).noquote()
        << QString("Structured tool call detected - name=%1, id=%2").arg(name, toolId);
}

void OllamaMessage::handleThinkingDelta(const QString &thinking)
{
    const int index = ensureThinkingContentIndex();
    if (auto *thinkingContent = blockAt<ThinkingContent>(index))
        thinkingContent->thinking += thinking;
}

void OllamaMessage::handleThinkingComplete(const QString &signature)
{
    if (auto *thinkingContent = blockAt<ThinkingContent>(lastIndexOfBlock<ThinkingContent>())) {
        thinkingContent->signature = signature;
        qCDebug(llmOllamaLog).noquote()
            << QString("Set thinking signature, length=%1").arg(signature.length());
    }
}

void OllamaMessage::handleStopReason(const QString &doneReason)
{
    const bool isToolCall = tryParseToolCall();

    if (!isToolCall && !m_contentAddedToTextBlock && !m_accumulatedContent.trimmed().isEmpty()) {
        const QString trimmed = stripMarkdownCodeFence(m_accumulatedContent);

        if (trimmed.startsWith('{')
            && (trimmed.contains("\"name\"") || trimmed.contains("\"arguments\""))) {
            qCDebug(llmOllamaLog).noquote()
                << QString("Skipping invalid/incomplete tool call JSON (length=%1)")
                       .arg(trimmed.size());

            removeBlocksIf([](const TurnContent &block) {
                const bool isText = std::get_if<TextContent>(&block) != nullptr;
                if (isText) {
                    qCDebug(llmOllamaLog).noquote()
                        << "Removing TextContent block (incomplete tool call)";
                }
                return isText;
            });

            m_accumulatedContent.clear();
        } else {
            if (auto *textContent = blockAt<TextContent>(ensureTextContentIndex()))
                textContent->text = m_accumulatedContent;
            m_contentAddedToTextBlock = true;
            qCDebug(llmOllamaLog).noquote()
                << QString("Added final accumulated content to TextContent, length=%1")
                       .arg(m_accumulatedContent.size());
        }
    }

    static const StopReasonMap kMap{{}, {}, {}, {}, MessageState::Final, true};
    recordStopReason(doneReason, kMap);
}
bool OllamaMessage::tryParseToolCall()
{
    QString trimmed = stripMarkdownCodeFence(m_accumulatedContent);

    if (trimmed.isEmpty() || !trimmed.startsWith('{')) {
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qCDebug(llmOllamaLog).noquote()
            << QString("Content starts with '{' but is not valid JSON: %1")
                   .arg(parseError.errorString());
        return false;
    }

    if (!doc.isObject()) {
        qCDebug(llmOllamaLog).noquote() << "Content is not a JSON object (not a tool call)";
        return false;
    }

    QJsonObject obj = doc.object();

    if (!obj.contains("name") || !obj.contains("arguments")) {
        qCDebug(llmOllamaLog).noquote()
            << "JSON missing 'name' or 'arguments' fields (not a tool call)";
        return false;
    }

    QString name = obj["name"].toString();
    QJsonValue argsValue = obj["arguments"];
    QJsonObject arguments;

    if (argsValue.isObject()) {
        arguments = argsValue.toObject();
    } else if (argsValue.isString()) {
        QJsonDocument argsDoc = QJsonDocument::fromJson(argsValue.toString().toUtf8());
        if (argsDoc.isObject()) {
            arguments = argsDoc.object();
        } else {
            qCDebug(llmOllamaLog).noquote() << "Failed to parse arguments as JSON object";
            return false;
        }
    } else {
        qCDebug(llmOllamaLog).noquote() << "Arguments field is neither object nor string";
        return false;
    }

    if (name.isEmpty()) {
        qCDebug(llmOllamaLog).noquote() << "Tool name is empty";
        return false;
    }

    QString toolId = makeToolCallId(name);

    for (const TurnContent &block : m_currentBlocks) {
        if (std::holds_alternative<TextContent>(block))
            qCDebug(llmOllamaLog).noquote() << "Removing TextContent block (tool call detected)";
    }
    clearBlocks();

    addCurrentContent(ToolUseContent{toolId, name, arguments});

    qCDebug(llmOllamaLog).noquote()
        << QString("Successfully parsed tool call from legacy format - name=%1, id=%2, args=%3")
               .arg(
                   name,
                   toolId,
                   QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact)));

    return true;
}

QString OllamaMessage::makeToolCallId(const QString &name)
{
    return QString("call_%1_%2").arg(name).arg(m_toolCallSequence++);
}

QString OllamaMessage::stripMarkdownCodeFence(const QString &content) const
{
    static const QRegularExpression fenceRegex(
        QStringLiteral(R"(^\s*```(?:\w+)?\s*\n?([\s\S]*?)\n?\s*```\s*$)"));

    QRegularExpressionMatch match = fenceRegex.match(content);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }
    return content.trimmed();
}

QJsonObject OllamaMessage::toProviderFormat() const
{
    return serializeTurn(TurnRole::Assistant, m_currentBlocks);
}

QJsonObject OllamaMessage::serializeTurn(TurnRole role, const QList<TurnContent> &blocks)
{
    const bool isAssistant = role == TurnRole::Assistant;

    QJsonObject message;
    message["role"] = isAssistant ? QStringLiteral("assistant") : QStringLiteral("user");

    QString textContent;
    QJsonArray toolCalls;
    QString thinkingContent;

    QJsonArray images;

    for (const TurnContent &block : blocks) {
        std::visit(
            detail::overloaded{
                [&](const TextContent &c) { textContent += c.text; },
                [&](const ImageContent &c) {
                    if (!c.isUrl())
                        images.append(c.base64());
                },
                [&](const AudioContent &) {},
                [&](const ToolUseContent &c) {
                    QJsonObject toolCall;
                    toolCall["type"] = "function";
                    toolCall["function"] = QJsonObject{{"name", c.name}, {"arguments", c.input}};
                    toolCalls.append(toolCall);
                },
                [&](const ToolResultContent &) {},
                [&](const ThinkingContent &c) { thinkingContent += c.thinking; },
                [&](const RedactedThinkingContent &) {}},
            block);
    }

    if (!images.isEmpty())
        message["images"] = images;

    if (!thinkingContent.isEmpty()) {
        message["thinking"] = thinkingContent;
    }

    if (!textContent.isEmpty() || !isAssistant) {
        message["content"] = textContent;
    }

    if (!toolCalls.isEmpty()) {
        message["tool_calls"] = toolCalls;
    }

    return message;
}

QJsonArray OllamaMessage::createToolResultMessages(
    const QHash<QString, ToolResult> &toolResults) const
{
    return mapToolResults(
        toolResults, [](const ToolUseContent &use, const ToolResult &r, QJsonArray &out) {
            const QString text = toolResultText(r);
            out.append(QJsonObject{{"role", "tool"}, {"content", text}});

            qCDebug(llmOllamaLog).noquote()
                << QString("Created tool result message for tool %1 (id=%2), content length=%3")
                       .arg(use.name, use.id)
                       .arg(text.length());
        });
}

bool OllamaMessage::isAccumulatingToolCall() const
{
    return !m_contentAddedToTextBlock && m_accumulatedContent.trimmed().startsWith('{');
}

void OllamaMessage::clearDerivedCaches()
{
    m_accumulatedContent.clear();
    m_contentAddedToTextBlock = false;
}

} // namespace LLMQore
