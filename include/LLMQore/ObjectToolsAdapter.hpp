// Copyright (C) 2026 Prashanth Udupa (prashanth@scrite.io)
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QMetaMethod>

#include <LLMQore/LLMQore_global.h>
#include <LLMQore/BaseTool.hpp>

namespace LLMQore {
class ToolRegistry;

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
        return create(new T(), props);
    }

    virtual ~ObjectToolsAdapter() override;

    QObject *object() const { return m_object; }

    QList<BaseTool *> registerTools(ToolRegistry *toolRegistry,
                                    MethodFilter filter = MethodFilter::AllPublic,
                                    const QString &toolPrefix = QString());

private:
    static ObjectToolsAdapter *create(QObject *object, const QVariantMap &props = QVariantMap());

    explicit ObjectToolsAdapter(QObject *object);

private:
    QObject *m_object = nullptr;
    const QMetaObject *m_metaObject = nullptr;
};

}
