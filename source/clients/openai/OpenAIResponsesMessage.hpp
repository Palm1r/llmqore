// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class OpenAIResponsesMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit OpenAIResponsesMessage(QObject *parent = nullptr);

    static const ToolDialect &toolDialect();

    MessageEffects applyEvent(const QString &eventType, const QJsonObject &data);
    MessageEffects applyResponse(const QJsonObject &response);

    [[nodiscard]] static QList<QJsonObject> serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks, ReasoningPersistence reasoning);

    QList<QJsonObject> toItemsFormat(ReasoningPersistence reasoning) const;
    QJsonArray createToolResultItems(const QHash<QString, ToolResult> &toolResults) const;
    static QJsonObject toResponsesInnerBlock(const ToolContent &block);

    QString accumulatedText() const;

    bool hasToolCalls() const { return !currentToolUseContent().isEmpty(); }
    bool hasThinkingContent() const { return !currentThinkingContent().isEmpty(); }

private:
    void clearDerivedCaches() override;

    void handleContentDelta(const QString &text);
    void handleToolCallStart(const QString &callId, const QString &name);
    void handleToolCallDelta(const QString &callId, const QString &argumentsDelta);
    void handleToolCallComplete(const QString &callId, const QString &finalArguments = QString());
    void handleReasoningStart(const QString &itemId);
    void handleReasoningDelta(const QString &itemId, const QString &text);
    void handleReasoningEncryptedContent(const QString &itemId, const QString &encryptedContent);
    void handleStopReason(const QString &status);

    void applyOutputItem(const QJsonObject &item, MessageEffects &effects);
    void applyItemDone(const QJsonObject &data, MessageEffects &effects);
    void applyReasoningItem(
        const QString &itemId, const QJsonObject &item, const QString &placeholder);
    void applyTerminal(
        const QJsonObject &response, const QString &fallbackStatus, MessageEffects &effects);

    static QString aggregatedTextOf(const QJsonObject &response);
    static QString reasoningTextOf(const QJsonObject &item);

    ToolCallAccumulator<QString> m_toolCalls;
    QHash<QString, int> m_thinkingBlocks;
    QHash<QString, QString> m_itemIdToCallId;
};

} // namespace LLMQore
