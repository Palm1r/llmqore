// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/ToolResult.hpp>

namespace LLMQore {

class BaseClient;

class LLMQORE_EXPORT ToolLoopRunner : public QObject
{
    Q_OBJECT
public:
    static constexpr int kDefaultMaxRounds = 10;

    explicit ToolLoopRunner(BaseClient *client);

    int maxRounds() const noexcept;
    void setMaxRounds(int limit) noexcept;

    int rounds(const QString &requestId) const;

public slots:
    void handleToolsCompleted(
        const QString &requestId, const QHash<QString, LLMQore::ToolResult> &toolResults);

private:
    struct LoopState
    {
        int rounds = 0;
    };

    BaseClient *m_client = nullptr;
    QHash<QString, LoopState> m_loops;
    int m_maxRounds = kDefaultMaxRounds;
};

} // namespace LLMQore
