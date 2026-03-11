// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMCore/BaseMessage.hpp>

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

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, QString> &toolResults) const;

    void startNewContinuation() override;

private:
    QString m_finishReason;
    QHash<int, QString> m_pendingToolArguments;
    QHash<int, ToolUseContent *> m_toolCallByIndex;

    void updateStateFromFinishReason();
};

} // namespace LLMCore
