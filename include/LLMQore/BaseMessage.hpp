// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QSet>

#include <LLMQore/LLMQore_global.h>

#include <LLMQore/ContentBlocks.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

struct LLMQORE_EXPORT PendingThinkingNotification
{
    QString thinking;
    QString signature;
};

struct LLMQORE_EXPORT MessageEffects
{
    QString chunk;
    QString fullText;
    QString fallbackText;
    QJsonObject usage;
    bool thinkingCompleted = false;
    bool toolsReady = false;
};

class LLMQORE_EXPORT BaseMessage : public QObject
{
    Q_OBJECT
public:
    explicit BaseMessage(QObject *parent = nullptr);
    ~BaseMessage() override;

    MessageState state() const { return m_state; }
    const QList<TurnContent> &currentBlocks() const { return m_currentBlocks; }

    virtual QString stopReason() const { return {}; }

    QList<ToolUseContent> currentToolUseContent() const;
    QList<ThinkingContent> currentThinkingContent() const;

    QList<PendingThinkingNotification> takePendingThinkingNotifications();

    virtual void startNewContinuation();

protected:
    using ToolResultEmitter
        = std::function<void(const ToolUseContent &, const ToolResult &, QJsonArray &)>;
    [[nodiscard]] QJsonArray mapToolResults(
        const QHash<QString, ToolResult> &toolResults, const ToolResultEmitter &emitFor) const;

    MessageState m_state = MessageState::Building;
    QList<TurnContent> m_currentBlocks;

    int getOrCreateTextContentIndex();
    void appendTextDelta(const QString &delta);

    void removeBlocksIf(const std::function<bool(const TurnContent &)> &predicate);
    void clearBlocks();

    template<typename T>
    int addCurrentContent(T &&content)
    {
        m_currentBlocks.append(TurnContent{std::forward<T>(content)});
        return m_currentBlocks.size() - 1;
    }

    template<typename T>
    [[nodiscard]] T *blockAt(int index)
    {
        if (index < 0 || index >= m_currentBlocks.size())
            return nullptr;
        return std::get_if<T>(&m_currentBlocks[index]);
    }

    template<typename T>
    [[nodiscard]] const T *blockAt(int index) const
    {
        if (index < 0 || index >= m_currentBlocks.size())
            return nullptr;
        return std::get_if<T>(&m_currentBlocks[index]);
    }

    template<typename T>
    [[nodiscard]] int lastIndexOfBlock() const
    {
        for (int i = m_currentBlocks.size() - 1; i >= 0; --i) {
            if (std::holds_alternative<T>(m_currentBlocks[i]))
                return i;
        }
        return -1;
    }

private:
    QSet<int> m_notifiedThinking;
};

} // namespace LLMQore
