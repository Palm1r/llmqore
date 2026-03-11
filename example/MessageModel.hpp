// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>

class MessageModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { RoleRole = Qt::UserRole + 1, TextRole };

    explicit MessageModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {}

    int rowCount(const QModelIndex & = {}) const override { return m_messages.size(); }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_messages.size())
            return {};
        const auto &msg = m_messages[index.row()];
        if (role == RoleRole)
            return msg.role;
        if (role == TextRole)
            return msg.text;
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {{RoleRole, "role"}, {TextRole, "text"}};
    }

    void append(const QString &role, const QString &text)
    {
        beginInsertRows({}, m_messages.size(), m_messages.size());
        m_messages.append({role, text});
        endInsertRows();
    }

    void appendToLast(const QString &chunk)
    {
        if (m_messages.isEmpty())
            return;
        m_messages.last().text += chunk;
        auto idx = index(m_messages.size() - 1);
        emit dataChanged(idx, idx, {TextRole});
    }

    void appendOrCreate(const QString &role, const QString &chunk)
    {
        if (!m_messages.isEmpty() && m_messages.last().role == role) {
            m_messages.last().text += chunk;
            auto idx = index(m_messages.size() - 1);
            emit dataChanged(idx, idx, {TextRole});
        } else {
            append(role, chunk);
        }
    }

    Q_INVOKABLE QString roleAt(int index) const
    {
        if (index < 0 || index >= m_messages.size())
            return {};
        return m_messages[index].role;
    }

    void clear()
    {
        beginResetModel();
        m_messages.clear();
        endResetModel();
    }

private:
    struct Message
    {
        QString role;
        QString text;
    };
    QList<Message> m_messages;
};
