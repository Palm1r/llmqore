// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/BaseMessage.hpp>

namespace LLMCore {

BaseMessage::BaseMessage(QObject *parent)
    : QObject(parent)
{}

BaseMessage::~BaseMessage()
{
    qDeleteAll(m_currentBlocks);
}

QList<ToolUseContent *> BaseMessage::getCurrentToolUseContent() const
{
    QList<ToolUseContent *> toolBlocks;
    for (auto *block : m_currentBlocks) {
        if (auto *toolContent = dynamic_cast<ToolUseContent *>(block)) {
            toolBlocks.append(toolContent);
        }
    }
    return toolBlocks;
}

QList<ThinkingContent *> BaseMessage::getCurrentThinkingContent() const
{
    QList<ThinkingContent *> thinkingBlocks;
    for (auto *block : m_currentBlocks) {
        if (auto *thinkingContent = dynamic_cast<ThinkingContent *>(block)) {
            thinkingBlocks.append(thinkingContent);
        }
    }
    return thinkingBlocks;
}

void BaseMessage::startNewContinuation()
{
    qDeleteAll(m_currentBlocks);
    m_currentBlocks.clear();
    m_state = MessageState::Building;
}

TextContent *BaseMessage::getOrCreateTextContent()
{
    for (auto *block : m_currentBlocks) {
        if (auto *textContent = dynamic_cast<TextContent *>(block)) {
            return textContent;
        }
    }

    return addCurrentContent<TextContent>();
}

} // namespace LLMCore
