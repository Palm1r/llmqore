// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMCore/BaseMessage.hpp>
#include <LLMCore/ToolResult.hpp>

namespace LLMCore {

class LLMCORE_EXPORT OllamaMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit OllamaMessage(QObject *parent = nullptr);

    void handleContentDelta(const QString &content);
    void handleToolCall(const QJsonObject &toolCall);
    void handleThinkingDelta(const QString &thinking);
    void handleThinkingComplete(const QString &signature);
    void handleDone(bool done);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultMessages(const QHash<QString, ToolResult> &toolResults) const;

    void startNewContinuation() override;

private:
    bool m_done = false;
    QString m_accumulatedContent;
    bool m_contentAddedToTextBlock = false;
    ThinkingContent *m_currentThinkingContent = nullptr;

    void updateStateFromDone();
    bool tryParseToolCall();
    bool isLikelyToolCallJson(const QString &content) const;
    ThinkingContent *getOrCreateThinkingContent();
};

} // namespace LLMCore
