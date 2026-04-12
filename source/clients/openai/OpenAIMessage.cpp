// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "OpenAIMessage.hpp"

#include <LLMCore/Log.hpp>

#include <QJsonArray>
#include <QJsonDocument>

namespace LLMCore {

OpenAIMessage::OpenAIMessage(QObject *parent)
    : BaseMessage(parent)
{}

void OpenAIMessage::handleContentDelta(const QString &content)
{
    auto textContent = getOrCreateTextContent();
    textContent->appendText(content);
}

void OpenAIMessage::handleToolCallStart(int index, const QString &id, const QString &name)
{
    qCDebug(llmOpenAILog).noquote()
        << QString("handleToolCallStart index=%1, id=%2, name=%3").arg(index).arg(id, name);

    auto *toolContent = addCurrentContent<ToolUseContent>(id, name);
    m_toolCallByIndex[index] = toolContent;
    m_pendingToolArguments[index] = "";
}

void OpenAIMessage::handleToolCallDelta(int index, const QString &argumentsDelta)
{
    if (m_pendingToolArguments.contains(index)) {
        m_pendingToolArguments[index] += argumentsDelta;
    }
}

void OpenAIMessage::handleToolCallComplete(int index)
{
    if (!m_pendingToolArguments.contains(index))
        return;

    QString jsonArgs = m_pendingToolArguments.take(index);
    QJsonObject argsObject;

    if (!jsonArgs.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(jsonArgs.toUtf8());
        if (doc.isObject())
            argsObject = doc.object();
    }

    if (auto *toolContent = m_toolCallByIndex.value(index))
        toolContent->setInput(argsObject);

    m_toolCallByIndex.remove(index);
}

void OpenAIMessage::completeAllPendingToolCalls()
{
    const auto indices = m_pendingToolArguments.keys();
    for (int index : indices)
        handleToolCallComplete(index);
}

void OpenAIMessage::handleFinishReason(const QString &finishReason)
{
    m_finishReason = finishReason;
    updateStateFromFinishReason();
}

QJsonObject OpenAIMessage::toProviderFormat() const
{
    QJsonObject message;
    message["role"] = "assistant";

    QString textContent;
    QJsonArray toolCalls;

    for (const auto *block : m_currentBlocks) {
        if (const auto *text = dynamic_cast<const TextContent *>(block)) {
            textContent += text->text();
        } else if (const auto *tool = dynamic_cast<const ToolUseContent *>(block)) {
            QJsonDocument doc(tool->input());
            toolCalls.append(
                QJsonObject{
                    {"id", tool->id()},
                    {"type", "function"},
                    {"function",
                     QJsonObject{
                         {"name", tool->name()},
                         {"arguments", QString::fromUtf8(doc.toJson(QJsonDocument::Compact))}}}});
        }
    }

    if (!textContent.isEmpty()) {
        message["content"] = textContent;
    } else {
        message["content"] = QJsonValue();
    }

    if (!toolCalls.isEmpty()) {
        message["tool_calls"] = toolCalls;
    }

    return message;
}

QJsonArray OpenAIMessage::createToolResultMessages(
    const QHash<QString, ToolResult> &toolResults) const
{
    QJsonArray messages;

    for (const auto *toolContent : getCurrentToolUseContent()) {
        if (toolResults.contains(toolContent->id())) {
            messages.append(
                QJsonObject{
                    {"role", "tool"},
                    {"tool_call_id", toolContent->id()},
                    {"content", toolResults[toolContent->id()].asText()}});
        }
    }

    return messages;
}

void OpenAIMessage::startNewContinuation()
{
    qCDebug(llmOpenAILog).noquote() << "Starting new continuation";

    m_toolCallByIndex.clear();

    BaseMessage::startNewContinuation();
    m_pendingToolArguments.clear();
    m_finishReason.clear();
}

void OpenAIMessage::updateStateFromFinishReason()
{
    if (m_finishReason == "tool_calls" && !getCurrentToolUseContent().empty()) {
        m_state = MessageState::RequiresToolExecution;
    } else if (m_finishReason == "stop") {
        m_state = MessageState::Final;
    } else {
        m_state = MessageState::Complete;
    }
}

} // namespace LLMCore
