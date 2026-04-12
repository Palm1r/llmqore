// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpTypes.hpp>

namespace LLMCore {
class ToolsManager;
} // namespace LLMCore

namespace LLMCore::Mcp {

class LLMCORE_EXPORT McpToolBinder : public QObject
{
    Q_OBJECT
public:
    McpToolBinder(McpClient *client, LLMCore::ToolsManager *target, QObject *parent = nullptr);

    QFuture<void> bind();

    QStringList registeredToolNames() const { return m_currentlyRegistered; }

signals:
    void bound(int toolCount);
    void failed(const QString &error);

private slots:
    void refreshTools();

private:
    void registerToolsFromList(const QList<ToolInfo> &tools);

    QPointer<McpClient> m_client;
    QPointer<LLMCore::ToolsManager> m_target;
    QStringList m_currentlyRegistered;
};

} // namespace LLMCore::Mcp
