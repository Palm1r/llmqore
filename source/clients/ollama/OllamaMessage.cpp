// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "OllamaMessage.hpp"
#include <LLMCore/Log.hpp>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

namespace LLMCore {

OllamaMessage::OllamaMessage(QObject *parent)
    : BaseMessage(parent)
{}

void OllamaMessage::handleContentDelta(const QString &content)
{
    m_accumulatedContent += content;
    QString trimmed = m_accumulatedContent.trimmed();

    if (trimmed.startsWith('{')) {
        return;
    }

    if (!m_contentAddedToTextBlock) {
        TextContent *textContent = getOrCreateTextContent();
        textContent->setText(m_accumulatedContent);
        m_contentAddedToTextBlock = true;
        qCDebug(llmOllamaLog).noquote()
            << QString("Added accumulated content to TextContent, length=%1")
                   .arg(m_accumulatedContent.length());
    } else {
        TextContent *textContent = getOrCreateTextContent();
        textContent->appendText(content);
    }
}

void OllamaMessage::handleToolCall(const QJsonObject &toolCall)
{
    QJsonObject function = toolCall["function"].toObject();
    QString name = function["name"].toString();
    QJsonObject arguments = function["arguments"].toObject();

    QString toolId = QString("call_%1_%2").arg(name).arg(QDateTime::currentMSecsSinceEpoch());

    if (!m_contentAddedToTextBlock && !m_accumulatedContent.trimmed().isEmpty()) {
        qCDebug(llmOllamaLog).noquote()
            << QString("Clearing accumulated content (tool call detected), length=%1")
                   .arg(m_accumulatedContent.length());
        m_accumulatedContent.clear();
    }

    addCurrentContent<ToolUseContent>(toolId, name, arguments);

    qCDebug(llmOllamaLog).noquote()
        << QString("Structured tool call detected - name=%1, id=%2").arg(name, toolId);
}

void OllamaMessage::handleThinkingDelta(const QString &thinking)
{
    ThinkingContent *thinkingContent = getOrCreateThinkingContent();
    thinkingContent->appendThinking(thinking);
}

void OllamaMessage::handleThinkingComplete(const QString &signature)
{
    if (m_currentThinkingContent) {
        m_currentThinkingContent->setSignature(signature);
        qCDebug(llmOllamaLog).noquote()
            << QString("Set thinking signature, length=%1").arg(signature.length());
    }
}

void OllamaMessage::handleDone(bool done)
{
    m_done = done;
    if (done) {
        bool isToolCall = tryParseToolCall();

        if (!isToolCall && !m_contentAddedToTextBlock && !m_accumulatedContent.trimmed().isEmpty()) {
            QString trimmed = m_accumulatedContent.trimmed();

            if (trimmed.startsWith('{')
                && (trimmed.contains("\"name\"") || trimmed.contains("\"arguments\""))) {
                qCDebug(llmOllamaLog).noquote()
                    << QString("Skipping invalid/incomplete tool call JSON (length=%1)")
                           .arg(trimmed.length());

                for (auto it = m_currentBlocks.begin(); it != m_currentBlocks.end();) {
                    if (dynamic_cast<TextContent *>(*it)) {
                        qCDebug(llmOllamaLog).noquote()
                            << "Removing TextContent block (incomplete tool call)";
                        delete *it;
                        it = m_currentBlocks.erase(it);
                    } else {
                        ++it;
                    }
                }

                m_accumulatedContent.clear();
            } else {
                TextContent *textContent = getOrCreateTextContent();
                textContent->setText(m_accumulatedContent);
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
    QString trimmed = m_accumulatedContent.trimmed();

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

    QString toolId = QString("call_%1_%2").arg(name).arg(QDateTime::currentMSecsSinceEpoch());

    for (auto *block : m_currentBlocks) {
        if (dynamic_cast<TextContent *>(block))
            qCDebug(llmOllamaLog).noquote() << "Removing TextContent block (tool call detected)";
    }
    qDeleteAll(m_currentBlocks);
    m_currentBlocks.clear();

    addCurrentContent<ToolUseContent>(toolId, name, arguments);

    qCDebug(llmOllamaLog).noquote()
        << QString("Successfully parsed tool call from legacy format - name=%1, id=%2, args=%3")
               .arg(
                   name,
                   toolId,
                   QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact)));

    return true;
}

bool OllamaMessage::isLikelyToolCallJson(const QString &content) const
{
    QString trimmed = content.trimmed();

    if (trimmed.startsWith('{')) {
        if (trimmed.contains("\"name\"") && trimmed.contains("\"arguments\"")) {
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);

            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj.contains("name") && obj.contains("arguments")) {
                    return true;
                }
            }
        }
    }

    return false;
}

QJsonObject OllamaMessage::toProviderFormat() const
{
    QJsonObject message;
    message["role"] = "assistant";

    QString textContent;
    QJsonArray toolCalls;
    QString thinkingContent;

    for (const auto *block : m_currentBlocks) {
        if (!block)
            continue;

        if (const auto *text = dynamic_cast<const TextContent *>(block)) {
            textContent += text->text();
        } else if (const auto *tool = dynamic_cast<const ToolUseContent *>(block)) {
            QJsonObject toolCall;
            toolCall["type"] = "function";
            toolCall["function"] = QJsonObject{{"name", tool->name()}, {"arguments", tool->input()}};
            toolCalls.append(toolCall);
        } else if (const auto *thinking = dynamic_cast<const ThinkingContent *>(block)) {
            thinkingContent += thinking->thinking();
        }
    }

    if (!thinkingContent.isEmpty()) {
        message["thinking"] = thinkingContent;
    }

    if (!textContent.isEmpty()) {
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
    QJsonArray messages;

    for (const auto *toolContent : getCurrentToolUseContent()) {
        if (toolResults.contains(toolContent->id())) {
            const QString text = toolResults[toolContent->id()].asText();
            QJsonObject toolMessage;
            toolMessage["role"] = "tool";
            toolMessage["content"] = text;
            messages.append(toolMessage);

            qCDebug(llmOllamaLog).noquote()
                << QString("Created tool result message for tool %1 (id=%2), content length=%3")
                       .arg(toolContent->name(), toolContent->id())
                       .arg(text.length());
        }
    }

    return messages;
}

void OllamaMessage::startNewContinuation()
{
    qCDebug(llmOllamaLog).noquote() << "Starting new continuation";

    BaseMessage::startNewContinuation();
    m_accumulatedContent.clear();
    m_done = false;
    m_contentAddedToTextBlock = false;
    m_currentThinkingContent = nullptr;
}

void OllamaMessage::updateStateFromDone()
{
    if (!getCurrentToolUseContent().empty()) {
        m_state = MessageState::RequiresToolExecution;
        qCDebug(llmOllamaLog).noquote()
            << QString("State set to RequiresToolExecution, tools count=%1")
                   .arg(getCurrentToolUseContent().size());
    } else {
        m_state = MessageState::Final;
        qCDebug(llmOllamaLog).noquote() << "State set to Final";
    }
}

ThinkingContent *OllamaMessage::getOrCreateThinkingContent()
{
    if (m_currentThinkingContent) {
        return m_currentThinkingContent;
    }

    for (auto *block : m_currentBlocks) {
        if (auto *thinkingContent = dynamic_cast<ThinkingContent *>(block)) {
            m_currentThinkingContent = thinkingContent;
            return m_currentThinkingContent;
        }
    }

    m_currentThinkingContent = addCurrentContent<ThinkingContent>();
    qCDebug(llmOllamaLog).noquote() << "Created new ThinkingContent block";
    return m_currentThinkingContent;
}

} // namespace LLMCore
