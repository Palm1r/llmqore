// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMCore/BaseMessage.hpp>
#include <LLMCore/ToolResult.hpp>

namespace LLMCore {

class LLMCORE_EXPORT OpenAIMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit OpenAIMessage(QObject *parent = nullptr);

    void handleContentDelta(const QString &content);
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

    void updateStateFromFinishReason();
};

} // namespace LLMCore
