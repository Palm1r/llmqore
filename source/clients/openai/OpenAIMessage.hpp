// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class LLMQORE_EXPORT OpenAIMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit OpenAIMessage(QObject *parent = nullptr);

    void handleContentDelta(const QString &content);
    void handleReasoningDelta(const QString &reasoning);
    QString currentThinking() const;
    void handleToolCallStart(int index, const QString &id, const QString &name);
    void handleToolCallDelta(int index, const QString &argumentsDelta);
    void handleToolCallComplete(int index);
    void completeAllPendingToolCalls();
    void handleFinishReason(const QString &finishReason);

    QString stopReason() const override { return m_finishReason; }

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, ToolResult> &toolResults) const;

    void startNewContinuation() override;

private:
    QString m_finishReason;
    QHash<int, QString> m_pendingToolArguments;
    QHash<int, ToolUseContent *> m_toolCallByIndex;
    ThinkingContent *m_currentThinkingContent = nullptr;

    void updateStateFromFinishReason();
    ThinkingContent *getOrCreateThinkingContent();
};

} // namespace LLMQore
