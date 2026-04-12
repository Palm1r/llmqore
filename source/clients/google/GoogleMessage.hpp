// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMCore/BaseMessage.hpp>
#include <LLMCore/ToolResult.hpp>

namespace LLMCore {

class LLMCORE_EXPORT GoogleMessage : public BaseMessage
{
    Q_OBJECT
public:
    explicit GoogleMessage(QObject *parent = nullptr);

    void handleContentDelta(const QString &text);
    void handleThoughtDelta(const QString &text);
    void handleThoughtSignature(const QString &signature);
    void handleFunctionCallStart(const QString &name);
    void handleFunctionCallArgsDelta(const QString &argsJson);
    void handleFunctionCallComplete();
    void handleFinishReason(const QString &reason);

    QJsonObject toProviderFormat() const;
    QJsonArray createToolResultParts(const QHash<QString, ToolResult> &toolResults) const;

    QString finishReason() const { return m_finishReason; }
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

} // namespace LLMCore
