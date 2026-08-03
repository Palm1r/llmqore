// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QSet>
#include <QStringList>

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

struct LLMQORE_EXPORT StopReasonMap
{
    QStringList toolReasons;
    QStringList completeReasons;
    QStringList finalReasons;
    QStringList openReasons;
    MessageState fallback = MessageState::Complete;
    bool toolsOverrideReason = false;
};

struct LLMQORE_EXPORT ToolContentNaming
{
    QLatin1String textType;
    std::function<QJsonObject(const ImageContent &)> renderImage;
};

[[nodiscard]] LLMQORE_EXPORT QJsonObject
renderToolContent(const ToolContent &block, const ToolContentNaming &naming);

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

    void startNewContinuation();

protected:
    virtual void clearDerivedCaches();

    template<typename Key>
    struct ToolCallAccumulator
    {
        QHash<Key, int> blockIndex;
        QHash<Key, QString> pending;

        void start(const Key &key, int index)
        {
            blockIndex[key] = index;
            pending[key] = QString();
        }

        void delta(const Key &key, const QString &fragment)
        {
            auto it = pending.find(key);
            if (it != pending.end())
                *it += fragment;
        }

        [[nodiscard]] bool isOpen(const Key &key) const { return pending.contains(key); }

        void clear()
        {
            blockIndex.clear();
            pending.clear();
        }
    };

    [[nodiscard]] static QJsonObject parseToolArguments(const QString &json);

    template<typename Key>
    void completeToolCall(
        ToolCallAccumulator<Key> &accumulator, const Key &key, const QString &finalArguments = {})
    {
        auto it = accumulator.pending.find(key);
        if (it == accumulator.pending.end())
            return;

        const QString json = finalArguments.isEmpty() ? *it : finalArguments;
        accumulator.pending.erase(it);
        const int index = accumulator.blockIndex.take(key);

        // Nothing was streamed for this call: whatever the opening event carried is
        // already the complete input, and overwriting it would empty a buffered turn.
        if (json.isEmpty())
            return;

        if (auto *tool = blockAt<ToolUseContent>(index))
            tool->input = parseToolArguments(json);
    }

    template<typename Key>
    void completeAllToolCalls(ToolCallAccumulator<Key> &accumulator)
    {
        const QList<Key> open = accumulator.pending.keys();
        for (const Key &key : open)
            completeToolCall(accumulator, key);
    }

    [[nodiscard]] MessageState resolveState(const QString &reason, const StopReasonMap &map) const;

    using ToolResultEmitter
        = std::function<void(const ToolUseContent &, const ToolResult &, QJsonArray &)>;
    [[nodiscard]] QJsonArray mapToolResults(
        const QHash<QString, ToolResult> &toolResults, const ToolResultEmitter &emitFor) const;

    MessageState m_state = MessageState::Building;
    QList<TurnContent> m_currentBlocks;

    int getOrCreateTextContentIndex();
    int getOrCreateThinkingContentIndex();
    void appendTextDelta(const QString &delta);

    int m_currentThinkingIndex = -1;

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
