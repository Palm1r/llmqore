// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/BaseTool.hpp>
#include <LLMCore/Log.hpp>
#include <LLMCore/ToolRegistry.hpp>

namespace LLMCore {

ToolRegistry::ToolRegistry(QObject *parent)
    : QObject(parent)
{}

void ToolRegistry::addTool(BaseTool *tool)
{
    if (!tool) {
        qCWarning(llmToolsLog).noquote() << "Attempted to add null tool";
        return;
    }

    const QString toolName = tool->id();
    if (m_tools.contains(toolName)) {
        qCDebug(llmToolsLog).noquote()
            << QString("Tool '%1' already registered, replacing").arg(toolName);
    }

    tool->setParent(this);
    m_tools.insert(toolName, tool);
    qCDebug(llmToolsLog).noquote() << QString("Added tool '%1'").arg(toolName);
    emit toolsChanged();
}

void ToolRegistry::removeTool(const QString &name)
{
    if (auto *t = m_tools.take(name)) {
        t->deleteLater();
        qCDebug(llmToolsLog).noquote() << QString("Removed tool '%1'").arg(name);
        emit toolsChanged();
    }
}

void ToolRegistry::removeAllTools()
{
    if (m_tools.isEmpty())
        return;
    for (auto *t : std::as_const(m_tools))
        t->deleteLater();
    m_tools.clear();
    qCDebug(llmToolsLog).noquote() << "Removed all tools";
    emit toolsChanged();
}

void ToolRegistry::removeToolsIf(std::function<bool(const BaseTool *)> predicate)
{
    bool changed = false;
    for (auto it = m_tools.begin(); it != m_tools.end();) {
        if (predicate(it.value())) {
            qCDebug(llmToolsLog).noquote() << QString("Removed tool '%1'").arg(it.key());
            it.value()->deleteLater();
            it = m_tools.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        emit toolsChanged();
}

BaseTool *ToolRegistry::tool(const QString &name) const
{
    return m_tools.value(name, nullptr);
}

QList<BaseTool *> ToolRegistry::registeredTools() const
{
    return m_tools.values();
}

} // namespace LLMCore
