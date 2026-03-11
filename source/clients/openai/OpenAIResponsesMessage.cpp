// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "OpenAIResponsesMessage.hpp"

#include <LLMCore/Log.hpp>

#include <QJsonArray>

namespace LLMCore {

OpenAIResponsesMessage::OpenAIResponsesMessage(QObject *parent)
    : BaseMessage(parent)
{}

void OpenAIResponsesMessage::handleContentDelta(const QString &text)
{
    if (!text.isEmpty()) {
        auto textItem = getOrCreateTextItem();
        textItem->appendText(text);
    }
}

void OpenAIResponsesMessage::handleToolCallStart(const QString &callId, const QString &name)
{
    auto *toolContent = addCurrentContent<ToolUseContent>(callId, name);
    m_toolCalls[callId] = toolContent;
    m_pendingToolArguments[callId] = "";
}

void OpenAIResponsesMessage::handleToolCallDelta(const QString &callId, const QString &argumentsDelta)
{
    if (m_pendingToolArguments.contains(callId)) {
        m_pendingToolArguments[callId] += argumentsDelta;
    }
}

void OpenAIResponsesMessage::handleToolCallComplete(const QString &callId)
{
    if (m_pendingToolArguments.contains(callId) && m_toolCalls.contains(callId)) {
        QString jsonArgs = m_pendingToolArguments[callId];
        QJsonObject argsObject;

        if (!jsonArgs.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonArgs.toUtf8());
            if (doc.isObject()) {
                argsObject = doc.object();
            }
        }

        m_toolCalls[callId]->setInput(argsObject);
        m_pendingToolArguments.remove(callId);
    }
}

void OpenAIResponsesMessage::handleReasoningStart(const QString &itemId)
{
    auto *thinkingContent = addCurrentContent<ThinkingContent>();
    m_thinkingBlocks[itemId] = thinkingContent;
}

void OpenAIResponsesMessage::handleReasoningDelta(const QString &itemId, const QString &text)
{
    if (m_thinkingBlocks.contains(itemId)) {
        m_thinkingBlocks[itemId]->appendThinking(text);
    }
}

void OpenAIResponsesMessage::handleReasoningComplete(const QString &itemId)
{
    Q_UNUSED(itemId);
}

void OpenAIResponsesMessage::handleStatus(const QString &status)
{
    m_status = status;
    updateStateFromStatus();
}

QList<QJsonObject> OpenAIResponsesMessage::toItemsFormat() const
{
    QList<QJsonObject> items;

    QString textContent;
    QList<const ToolUseContent *> toolCalls;

    for (const auto *block : m_currentBlocks) {
        if (const auto *text = dynamic_cast<const TextContent *>(block)) {
            textContent += text->text();
        } else if (const auto *tool = dynamic_cast<const ToolUseContent *>(block)) {
            toolCalls.append(tool);
        }
    }

    if (!textContent.isEmpty()) {
        QJsonObject message;
        message["role"] = "assistant";
        message["content"] = textContent;
        items.append(message);
    }

    for (const auto *tool : toolCalls) {
        QJsonObject functionCallItem;
        functionCallItem["type"] = "function_call";
        functionCallItem["call_id"] = tool->id();
        functionCallItem["name"] = tool->name();
        functionCallItem["arguments"] = QString::fromUtf8(
            QJsonDocument(tool->input()).toJson(QJsonDocument::Compact));
        items.append(functionCallItem);
    }

    return items;
}

QJsonArray OpenAIResponsesMessage::createToolResultItems(
    const QHash<QString, QString> &toolResults) const
{
    QJsonArray items;

    for (const auto *toolContent : getCurrentToolUseContent()) {
        if (toolResults.contains(toolContent->id())) {
            QJsonObject toolResultItem;
            toolResultItem["type"] = "function_call_output";
            toolResultItem["call_id"] = toolContent->id();
            toolResultItem["output"] = toolResults[toolContent->id()];
            items.append(toolResultItem);
        }
    }

    return items;
}

QString OpenAIResponsesMessage::accumulatedText() const
{
    QString text;
    for (const auto *block : m_currentBlocks) {
        if (const auto *textContent = dynamic_cast<const TextContent *>(block)) {
            text += textContent->text();
        }
    }
    return text;
}

void OpenAIResponsesMessage::updateStateFromStatus()
{
    if (m_status == "completed") {
        if (!getCurrentToolUseContent().isEmpty()) {
            m_state = MessageState::RequiresToolExecution;
        } else {
            m_state = MessageState::Complete;
        }
    } else if (m_status == "in_progress") {
        m_state = MessageState::Building;
    } else if (m_status == "failed" || m_status == "cancelled" || m_status == "incomplete") {
        m_state = MessageState::Final;
    } else {
        m_state = MessageState::Building;
    }
}

TextContent *OpenAIResponsesMessage::getOrCreateTextItem()
{
    for (auto *block : m_currentBlocks) {
        if (auto *textContent = dynamic_cast<TextContent *>(block)) {
            return textContent;
        }
    }

    return addCurrentContent<TextContent>();
}

void OpenAIResponsesMessage::startNewContinuation()
{
    m_toolCalls.clear();
    m_thinkingBlocks.clear();

    BaseMessage::startNewContinuation();

    m_pendingToolArguments.clear();
    m_status.clear();
}

} // namespace LLMCore
