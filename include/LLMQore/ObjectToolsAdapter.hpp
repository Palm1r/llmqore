// Copyright (C) 2026 Prashanth Udupa (prashanth@scrite.io)
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QMetaMethod>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/BaseTool.hpp>

namespace LLMQore {
class ToolRegistry;
class ObjectMethodTool;
class ObjectToolsAdapter;

class LLMQORE_EXPORT AbstractToolObject : public QObject
{
    Q_OBJECT

public:
    ~AbstractToolObject() override;

    QString name() const;

protected:
    friend class ObjectMethodTool;
    friend class ObjectToolsAdapter;

    virtual QString toolPrefix() const { return QString(); }

    virtual bool queryMethodInfo(const QMetaMethod &method, QString &id, QString &displayName,
                                 QString &description);

    explicit AbstractToolObject();

private:
    mutable QString m_name;
};

class LLMQORE_EXPORT ObjectToolsAdapter : public QObject
{
    Q_OBJECT

public:
    enum MethodFilter {
        PublicInvokable = 1,
        PublicSlot = 2,
        AllPublic = PublicInvokable | PublicSlot,
    };

    template<class T>
    static ObjectToolsAdapter *create(const QVariantMap &props = QVariantMap())
    {
        if (T::staticMetaObject.superClass() == &AbstractToolObject::staticMetaObject)
            return create(new T(), props);

        return nullptr;
    }

    virtual ~ObjectToolsAdapter() override;

    QObject *object() const { return m_object; }

    QList<BaseTool *> registerTools(ToolRegistry *toolRegistry,
                                    MethodFilter filter = MethodFilter::AllPublic);

private:
    static ObjectToolsAdapter *create(AbstractToolObject *object,
                                      const QVariantMap &props = QVariantMap());

    explicit ObjectToolsAdapter(AbstractToolObject *object);

private:
    AbstractToolObject *m_object = nullptr;
    const QMetaObject *m_metaObject = nullptr;
};

}
