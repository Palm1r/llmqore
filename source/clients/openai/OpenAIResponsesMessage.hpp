// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMCore/BaseMessage.hpp>
#include <LLMCore/ToolResult.hpp>

namespace LLMCore {

class LLMCORE_EXPORT OpenAIResponsesMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit OpenAIResponsesMessage(QObject *parent = nullptr);

    void handleContentDelta(const QString &text);
    void handleToolCallStart(const QString &callId, const QString &name);
    void handleToolCallDelta(const QString &callId, const QString &argumentsDelta);
    void handleToolCallComplete(const QString &callId);
    void handleReasoningStart(const QString &itemId);
    void handleReasoningDelta(const QString &itemId, const QString &text);
    void handleReasoningComplete(const QString &itemId);
    void handleStatus(const QString &status);

    QList<QJsonObject> toItemsFormat() const;
    QJsonArray createToolResultItems(const QHash<QString, ToolResult> &toolResults) const;

    QString accumulatedText() const;

    bool hasToolCalls() const noexcept { return !m_toolCalls.isEmpty(); }
    bool hasThinkingContent() const noexcept { return !m_thinkingBlocks.isEmpty(); }

    void startNewContinuation() override;

private:
    QString m_status;
    QHash<QString, QString> m_pendingToolArguments;
    QHash<QString, ToolUseContent *> m_toolCalls;
    QHash<QString, ThinkingContent *> m_thinkingBlocks;

    void updateStateFromStatus();
    TextContent *getOrCreateTextItem();
};

} // namespace LLMCore
