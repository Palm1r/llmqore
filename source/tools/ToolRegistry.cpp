// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/ToolRegistry.hpp>

#include <QThread>

namespace LLMQore {

// All ToolRegistry methods below mutate or expose raw pointers to m_tools
// and must run on the owning thread. toolsSnapshot() is also owning-thread
// only (it reads m_tools), but the value it returns is safe to pass across
// thread boundaries.
namespace {
inline void assertOwningThread(const QObject *self)
{
    Q_ASSERT_X(self->thread() == QThread::currentThread(),
               "LLMQore::ToolRegistry",
               "ToolRegistry accessed from non-owning thread");
}
} // namespace

ToolRegistry::ToolRegistry(QObject *parent)
    : QObject(parent)
{}

void ToolRegistry::addTool(BaseTool *tool)
{
    assertOwningThread(this);
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
    assertOwningThread(this);
    if (auto *t = m_tools.take(name)) {
        t->deleteLater();
        qCDebug(llmToolsLog).noquote() << QString("Removed tool '%1'").arg(name);
        emit toolsChanged();
    }
}

void ToolRegistry::removeAllTools()
{
    assertOwningThread(this);
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
    assertOwningThread(this);
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
    assertOwningThread(this);
    return m_tools.value(name, nullptr);
}

QList<BaseTool *> ToolRegistry::registeredTools() const
{
    assertOwningThread(this);
    return m_tools.values();
}

QList<ToolSnapshot> ToolRegistry::toolsSnapshot() const
{
    assertOwningThread(this);
    QList<ToolSnapshot> out;
    out.reserve(m_tools.size());
    for (auto *t : std::as_const(m_tools)) {
        if (!t)
            continue;
        // Force detach on every QString so the snapshot owns its storage
        // and cannot share COW buffers with live BaseTool fields.
        ToolSnapshot snap;
        snap.id = t->id();
        snap.id.detach();
        snap.displayName = t->displayName();
        snap.displayName.detach();
        snap.description = t->description();
        snap.description.detach();
        out.append(std::move(snap));
    }
    return out;
}

} // namespace LLMQore
