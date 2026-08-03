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

    void handleContentDelta(const QString &text);
    void handleThoughtDelta(const QString &text);
    void handleThoughtSignature(const QString &signature);
    void handleToolCallStart(const QString &name);
    void handleToolCallDelta(const QString &argsJson);
    void handleToolCallComplete();
    void handleStopReason(const QString &reason);

    [[nodiscard]] static QJsonObject serializeTurn(
        TurnRole role, const QList<TurnContent> &blocks);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultParts(const QHash<QString, ToolResult> &toolResults) const;
    static QJsonObject toInlineDataPart(const ToolContent &block);
    static QString toolResultTurnRole(const QJsonArray &parts);

    QString stopReason() const override { return m_finishReason; }
    bool isErrorFinishReason() const;
    QString getErrorMessage() const;
    void startNewContinuation() override;

private:
    void updateStateFromFinishReason();

    QString m_pendingFunctionArgs;
    QString m_currentFunctionName;
    QString m_finishReason;
};

} // namespace LLMQore
