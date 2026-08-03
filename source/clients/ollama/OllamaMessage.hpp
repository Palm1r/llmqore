// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/Conversation.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class OllamaMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit OllamaMessage(QObject *parent = nullptr);

    static const ToolDialect &toolDialect();

    void handleContentDelta(const QString &content);
    void handleToolCall(const QJsonObject &toolCall);
    void handleThinkingDelta(const QString &thinking);
    void handleThinkingComplete(const QString &signature);
    void handleStopReason(bool done, const QString &doneReason = {});

    QString stopReason() const override { return m_doneReason; }

    [[nodiscard]] static QJsonObject serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, ToolResult> &toolResults) const;

    bool isAccumulatingToolCall() const;

    void startNewContinuation() override;

private:
    bool m_done = false;
    QString m_doneReason;
    QString m_accumulatedContent;
    bool m_contentAddedToTextBlock = false;
    int m_currentThinkingIndex = -1;
    quint64 m_toolCallSequence = 0;

    QString makeToolCallId(const QString &name);
    void updateStateFromDone();
    bool tryParseToolCall();
    QString stripMarkdownCodeFence(const QString &content) const;
    int getOrCreateThinkingContentIndex();
};

} // namespace LLMQore
