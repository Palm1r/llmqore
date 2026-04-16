// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore {

class BaseTool;

// Detached snapshot of a tool's user-facing metadata. Safe to hold across
// registry mutations and safe to read from any thread — all QString members
// are independent copies at the moment toolsSnapshot() was called.
//
// Named `ToolSnapshot` to disambiguate from LLMQore::Mcp::ToolInfo (the MCP
// wire-protocol tool descriptor in McpTypes.hpp).
struct LLMQORE_EXPORT ToolSnapshot
{
    QString id;
    QString displayName;
    QString description;
};

// Thread contract: ToolRegistry (and its subclass ToolsManager) lives on
// the thread of its QObject parent. All mutating methods (addTool,
// removeTool, removeAllTools, removeToolsIf) and the raw-pointer accessor
// (registeredTools) MUST be called from that thread. External consumers
// that only need display metadata should prefer toolsSnapshot(), which
// is the only method safe to call once the registry has been handed out
// for display — it returns fully detached copies.
class LLMQORE_EXPORT ToolRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ToolRegistry(QObject *parent = nullptr);

    void addTool(BaseTool *tool);
    void removeTool(const QString &name);
    void removeAllTools();
    void removeToolsIf(std::function<bool(const BaseTool *)> predicate);
    BaseTool *tool(const QString &name) const;

    // Internal / owning-thread only. Returns raw pointers into the
    // registry, valid only until the next mutation. Prefer
    // toolsSnapshot() for UI / cross-boundary use.
    QList<BaseTool *> registeredTools() const;

    // Detached snapshot of all tools' display metadata. Safe to hand to
    // UI code on any thread; no pointers into the registry escape.
    QList<ToolSnapshot> toolsSnapshot() const;

signals:
    void toolsChanged();

protected:
    // QMap for deterministic alphabetical iteration order — important for
    // reproducible test output and stable round-trips on the wire.
    QMap<QString, BaseTool *> m_tools;
};

} // namespace LLMQore
