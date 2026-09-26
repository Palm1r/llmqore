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

    MessageEffects applyEvent(const QJsonObject &chunk);
    MessageEffects applyResponse(const QJsonObject &response);

    [[nodiscard]] static QJsonObject serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, ToolResult> &toolResults) const;

private:
    void clearDerivedCaches() override;

    QString takeReasoningAndText(const QJsonObject &source);
    void handleContentDelta(const QString &content);
    void handleReasoningDelta(const QString &reasoning);
    void handleToolCallStart(int index, const QString &id, const QString &name);
    void handleToolCallDelta(int index, const QString &argumentsDelta);
    void handleStopReason(const QString &finishReason);

    ToolCallAccumulator<int> m_toolCalls;
};

} // namespace LLMQore
