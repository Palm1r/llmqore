// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/AcpAgentRegistry.hpp>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <LLMQore/Log.hpp>

namespace LLMQore::Acp {

QJsonObject AcpAgentEntry::toJson() const
{
    QJsonObject o = config.toJson(); // command / args / env / cwd
    o.insert("id", id);
    o.insert("name", name);
    if (!description.isEmpty())
        o.insert("description", description);
    return o;
}

AcpAgentEntry AcpAgentEntry::fromJson(const QJsonObject &obj)
{
    AcpAgentEntry e;
    e.id = obj.value("id").toString();
    e.name = obj.value("name").toString();
    e.description = obj.value("description").toString();
    e.config = AcpAgentConfig::fromJson(obj);
    if (e.name.isEmpty())
        e.name = e.id;
    return e;
}

int AcpAgentRegistry::indexOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id)
            return i;
    }
    return -1;
}

void AcpAgentRegistry::add(const AcpAgentEntry &entry)
{
    if (entry.id.isEmpty())
        return;
    const int idx = indexOf(entry.id);
    if (idx >= 0)
        m_entries[idx] = entry; // override
    else
        m_entries.append(entry);
}

void AcpAgentRegistry::loadFromJson(const QJsonObject &obj)
{
    for (const QJsonValue &v : obj.value("agents").toArray()) {
        if (!v.isObject())
            continue;
        const AcpAgentEntry e = AcpAgentEntry::fromJson(v.toObject());
        if (!e.id.isEmpty())
            add(e);
    }
}

bool AcpAgentRegistry::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(llmAcpLog).noquote()
            << QString("AcpAgentRegistry: cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(llmAcpLog).noquote()
            << QString("AcpAgentRegistry: cannot parse %1: %2").arg(path, err.errorString());
        return false;
    }
    loadFromJson(doc.object());
    return true;
}

QStringList AcpAgentRegistry::ids() const
{
    QStringList out;
    out.reserve(m_entries.size());
    for (const AcpAgentEntry &e : m_entries)
        out.append(e.id);
    return out;
}

bool AcpAgentRegistry::contains(const QString &id) const
{
    return indexOf(id) >= 0;
}

std::optional<AcpAgentEntry> AcpAgentRegistry::entry(const QString &id) const
{
    const int idx = indexOf(id);
    if (idx < 0)
        return std::nullopt;
    return m_entries.at(idx);
}

std::optional<AcpAgentConfig> AcpAgentRegistry::config(
    const QString &id, const QString &cwd) const
{
    const int idx = indexOf(id);
    if (idx < 0)
        return std::nullopt;
    AcpAgentConfig c = m_entries.at(idx).config;
    if (!cwd.isEmpty())
        c.cwd = cwd;
    return c;
}

QJsonObject AcpAgentRegistry::toJson() const
{
    QJsonArray arr;
    for (const AcpAgentEntry &e : m_entries)
        arr.append(e.toJson());
    return QJsonObject{{"agents", arr}};
}

} // namespace LLMQore::Acp
