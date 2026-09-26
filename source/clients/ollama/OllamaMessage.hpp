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

    MessageEffects applyEvent(const QJsonObject &event);
    MessageEffects applyResponse(const QJsonObject &response);

    [[nodiscard]] static QJsonObject serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, ToolResult> &toolResults) const;

private:
    void clearDerivedCaches() override;

    void handleContentDelta(const QString &content);
    void handleToolCall(const QJsonObject &toolCall);
    void handleThinkingDelta(const QString &thinking);
    void handleThinkingComplete(const QString &signature);
    void handleStopReason(const QString &doneReason);
    [[nodiscard]] bool isAccumulatingToolCall() const;

    QString m_accumulatedContent;
    bool m_contentAddedToTextBlock = false;
    quint64 m_toolCallSequence = 0;

    QString makeToolCallId(const QString &name);
    bool tryParseToolCall();
    QString stripMarkdownCodeFence(const QString &content) const;
};

} // namespace LLMQore
