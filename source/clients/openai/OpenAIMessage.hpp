// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QJsonValue>

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/Conversation.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class OpenAIMessage : public BaseMessage
{
    Q_OBJECT
public:
    struct ContentParts
    {
        QString thinking;
        QString text;
    };

    [[nodiscard]] static ContentParts splitContentParts(const QJsonValue &content);

    explicit OpenAIMessage(QObject *parent = nullptr);

    static const ToolDialect &toolDialect();

    void handleContentDelta(const QString &content);
    void handleReasoningDelta(const QString &reasoning);
    void handleToolCallStart(int index, const QString &id, const QString &name);
    void handleToolCallDelta(int index, const QString &argumentsDelta);
    void handleToolCallComplete(int index);
    void completeAllPendingToolCalls();
    void handleStopReason(const QString &finishReason);

    QString stopReason() const override { return m_finishReason; }

    [[nodiscard]] static QJsonObject serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, ToolResult> &toolResults) const;

    void startNewContinuation() override;

private:
    QString m_finishReason;
    QHash<int, QString> m_pendingToolArguments;
    QHash<int, int> m_toolCallByIndex;
    int m_currentThinkingIndex = -1;

    void updateStateFromFinishReason();
    int getOrCreateThinkingContentIndex();
};

} // namespace LLMQore
