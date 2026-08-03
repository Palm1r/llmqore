// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class ClaudeMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit ClaudeMessage(QObject *parent = nullptr);

    // How this provider spells tool schemas on the way out.
    static const ToolDialect &toolDialect();

    struct Effects
    {
        QString chunk;
        QJsonObject usage;
        bool thinkingCompleted = false;
        bool toolsReady = false;
    };

    Effects applyEvent(const QJsonObject &event);
    Effects applyResponse(const QJsonObject &response);

    QString stopReason() const override { return m_stopReason; }

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultsContent(const QHash<QString, ToolResult> &toolResults) const;

    QList<RedactedThinkingContent> currentRedactedThinkingContent() const;

    static QJsonValue serializeTurnContent(const TurnContent &block);

    void startNewContinuation() override;

private:
    void beginBlock(int index, const QJsonObject &block);
    void applyDelta(int index, const QJsonObject &delta);
    void endBlock(int index);
    void applyStopReason(const QString &stopReason);

    QString m_stopReason;
    QHash<int, QString> m_pendingToolInputs;

    void updateStateFromStopReason();
};

} // namespace LLMQore
