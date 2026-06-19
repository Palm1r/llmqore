// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <LLMQore/AcpAgentConfig.hpp>
#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Acp {

struct LLMQORE_EXPORT AcpAgentEntry
{
    QString id;
    QString name;        // human-readable label
    QString description;
    AcpAgentConfig config; // command / args / env (cwd is set per session)

    QJsonObject toJson() const;
    static AcpAgentEntry fromJson(const QJsonObject &obj);
};

// A data-driven catalogue of ACP agents. The library ships no built-in agents:
// the host loads them from external JSON (a file, a Qt resource, or an in-memory
// object). Wire format:
//
//   { "agents": [
//       { "id": "claude", "name": "Claude Code", "command": "npx",
//         "args": ["-y", "@agentclientprotocol/claude-agent-acp"] },
//       ...
//   ]}
//
// Entries are merged/overridden by id, so later loads layer on top of earlier
// ones (built-in file, then user overrides).
class LLMQORE_EXPORT AcpAgentRegistry
{
public:
    void loadFromJson(const QJsonObject &obj);
    // Reads `path` (filesystem or ":/qrc") and merges it. Returns false if the
    // file cannot be opened or parsed.
    bool loadFromFile(const QString &path);

    bool isEmpty() const { return m_entries.isEmpty(); }
    int size() const { return m_entries.size(); }
    QStringList ids() const;
    QList<AcpAgentEntry> entries() const { return m_entries; }

    bool contains(const QString &id) const;
    std::optional<AcpAgentEntry> entry(const QString &id) const;

    // Launch config for `id` with `cwd` filled in, or nullopt if unknown.
    std::optional<AcpAgentConfig> config(const QString &id, const QString &cwd = {}) const;

    void add(const AcpAgentEntry &entry); // merge/override by id
    void clear() { m_entries.clear(); }

    QJsonObject toJson() const;

private:
    int indexOf(const QString &id) const;

    QList<AcpAgentEntry> m_entries; // insertion-ordered, unique by id
};

} // namespace LLMQore::Acp
