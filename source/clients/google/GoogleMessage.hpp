// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/Conversation.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class GoogleMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit GoogleMessage(QObject *parent = nullptr);

    static const ToolDialect &toolDialect();

    MessageEffects applyEvent(const QJsonObject &chunk);
    MessageEffects applyResponse(const QJsonObject &response);

    [[nodiscard]] static QJsonObject serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultParts(const QHash<QString, ToolResult> &toolResults) const;
    static QJsonObject toInlineDataPart(const ToolContent &block);
    static QString toolResultTurnRole(const QJsonArray &parts);

private:
    void clearDerivedCaches() override;

    void handleContentDelta(const QString &text);
    void handleThoughtDelta(const QString &text);
    void handleThoughtSignature(const QString &signature);
    void handleToolCallStart(const QString &name);
    void handleToolCallDelta(const QString &argsJson);
    void handleToolCallComplete();
    void handleStopReason(const QString &reason);

    [[nodiscard]] bool isErrorFinishReason() const;
    [[nodiscard]] QString errorFinishMessage() const;

    QString m_pendingFunctionArgs;
    QString m_currentFunctionName;
};

} // namespace LLMQore
