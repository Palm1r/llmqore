// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include "OllamaMessage.hpp"
#include <LLMQore/Log.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

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

void OllamaMessage::handleContentDelta(const QString &content)
{
    m_accumulatedContent += content;
    QString trimmed = m_accumulatedContent.trimmed();

    if (trimmed.startsWith('{') || trimmed.startsWith('`')) {
        return;
    }

    if (!m_contentAddedToTextBlock) {
        if (auto *textContent = blockAt<TextContent>(getOrCreateTextContentIndex()))
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
    const int index = getOrCreateThinkingContentIndex();
    if (auto *thinkingContent = blockAt<ThinkingContent>(index))
        thinkingContent->thinking += thinking;
}

void OllamaMessage::handleThinkingComplete(const QString &signature)
{
    if (auto *thinkingContent = blockAt<ThinkingContent>(m_currentThinkingIndex)) {
        thinkingContent->signature = signature;
        qCDebug(llmOllamaLog).noquote()
            << QString("Set thinking signature, length=%1").arg(signature.length());
    }
}

void OllamaMessage::handleStopReason(bool done, const QString &doneReason)
{
    m_done = done;
    if (!doneReason.isEmpty())
        m_doneReason = doneReason;
    if (done) {
        bool isToolCall = tryParseToolCall();

        if (!isToolCall && !m_contentAddedToTextBlock && !m_accumulatedContent.trimmed().isEmpty()) {
            QString trimmed = stripMarkdownCodeFence(m_accumulatedContent);

            if (trimmed.startsWith('{')
                && (trimmed.contains("\"name\"") || trimmed.contains("\"arguments\""))) {
                qCDebug(llmOllamaLog).noquote()
                    << QString("Skipping invalid/incomplete tool call JSON (length=%1)")
                           .arg(trimmed.length());

                removeBlocksIf([](const TurnContent &block) {
                    const bool isText = std::get_if<TextContent>(&block) != nullptr;
                    if (isText) {
                        qCDebug(llmOllamaLog).noquote()
                            << "Removing TextContent block (incomplete tool call)";
                    }
                    return isText;
                });
                m_currentThinkingIndex = -1;

                m_accumulatedContent.clear();
            } else {
                if (auto *textContent = blockAt<TextContent>(getOrCreateTextContentIndex()))
                    textContent->text = m_accumulatedContent;
                m_contentAddedToTextBlock = true;
                qCDebug(llmOllamaLog).noquote()
                    << QString("Added final accumulated content to TextContent, length=%1")
                           .arg(m_accumulatedContent.length());
            }
        }

        updateStateFromDone();
    }
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
    m_currentThinkingIndex = -1;

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

void OllamaMessage::startNewContinuation()
{
    qCDebug(llmOllamaLog).noquote() << "Starting new continuation";

    BaseMessage::startNewContinuation();
    m_accumulatedContent.clear();
    m_done = false;
    m_doneReason.clear();
    m_contentAddedToTextBlock = false;
    m_currentThinkingIndex = -1;
}

void OllamaMessage::updateStateFromDone()
{
    if (!currentToolUseContent().empty()) {
        m_state = MessageState::RequiresToolExecution;
        qCDebug(llmOllamaLog).noquote()
            << QString("State set to RequiresToolExecution, tools count=%1")
                   .arg(currentToolUseContent().size());
    } else {
        m_state = MessageState::Final;
        qCDebug(llmOllamaLog).noquote() << "State set to Final";
    }
}

int OllamaMessage::getOrCreateThinkingContentIndex()
{
    if (m_currentThinkingIndex >= 0)
        return m_currentThinkingIndex;

    for (int i = 0; i < m_currentBlocks.size(); ++i) {
        if (std::holds_alternative<ThinkingContent>(m_currentBlocks[i])) {
            m_currentThinkingIndex = i;
            return m_currentThinkingIndex;
        }
    }

    m_currentThinkingIndex = addCurrentContent(ThinkingContent{});
    qCDebug(llmOllamaLog).noquote() << "Created new ThinkingContent block";
    return m_currentThinkingIndex;
}

} // namespace LLMQore
