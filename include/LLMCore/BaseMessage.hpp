// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMCore/LLMCore_global.h>

#include <LLMCore/ContentBlocks.hpp>

namespace LLMCore {

class LLMCORE_EXPORT BaseMessage : public QObject
{
    Q_OBJECT
public:
    explicit BaseMessage(QObject *parent = nullptr);
    ~BaseMessage() override;

    MessageState state() const { return m_state; }
    const QList<ContentBlock *> &getCurrentBlocks() const { return m_currentBlocks; }

    virtual QString stopReason() const { return {}; }

    ContentBlock *blockAt(int index) const
    {
        if (index < 0 || index >= m_currentBlocks.size())
            return nullptr;
        return m_currentBlocks[index];
    }
    int blockCount() const { return m_currentBlocks.size(); }

    QList<ToolUseContent *> getCurrentToolUseContent() const;
    QList<ThinkingContent *> getCurrentThinkingContent() const;

    virtual void startNewContinuation();

protected:
    MessageState m_state = MessageState::Building;
    QList<ContentBlock *> m_currentBlocks;

    TextContent *getOrCreateTextContent();

    template<typename T, typename... Args>
    T *addCurrentContent(Args &&...args)
    {
        return addContentBlock<T>(m_currentBlocks, std::forward<Args>(args)...);
    }
};

} // namespace LLMCore
