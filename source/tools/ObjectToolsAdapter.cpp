// Copyright (C) 2026 Prashanth Udupa (prashanth@scrite.io)
// SPDX-License-Identifier: MIT

#include <LLMQore/ObjectToolsAdapter.hpp>
#include <LLMQore/ToolRegistry.hpp>

#include <QMetaType>
#include <QtConcurrentRun>
#include <QCoreApplication>

namespace LLMQore {

static QThread *ObjectToolsAdapterThread()
{
    static QThread *thread = nullptr;
    if (thread == nullptr) {
        thread = new QThread(qApp);
        thread->setObjectName("ObjectToolsAdapterThread");

        auto cleanup = [=]() {
            if (thread->isRunning()) {
                thread->quit();
                thread->wait();
            }
        };

        QObject::connect(qApp, &QCoreApplication::destroyed, thread, cleanup);
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, thread, cleanup);

        thread->start();
    }

    return thread;
}

AbstractToolObject::AbstractToolObject() : QObject(nullptr) { }

AbstractToolObject::~AbstractToolObject() { }

QString AbstractToolObject::name() const
{
    if (m_name.isEmpty()) {
        // Infer the object name
        const QMetaObject *metaObject = this->metaObject();
        if (this->objectName().isEmpty()) {
            m_name = QString::fromLatin1(metaObject->className());
        } else {
            m_name = this->objectName();
        }
    }

    return m_name;
}

bool AbstractToolObject::queryMethodInfo(const QMetaMethod &method, QString &id,
                                         QString &displayName, QString &description)
{
    // First verify that the method belongs to this object
    const QMetaObject *metaObject = this->metaObject();
    bool validMethod = false;
    while (metaObject) {
        if (method.enclosingMetaObject() == metaObject) {
            validMethod = true;
            break;
        }
        metaObject = metaObject->superClass();
    }
    metaObject = this->metaObject();

    if (!validMethod)
        return false;

    if ((method.methodType() != QMetaMethod::Method && method.methodType() != QMetaMethod::Slot)
        || method.access() != QMetaMethod::Public)
        return false;

    // ID is the method name without the tool prefix (if any)
    const QString prefix = this->toolPrefix();
    id = QString::fromLatin1(method.name());
    if (!prefix.isEmpty() && id.startsWith(prefix)) {
        id.remove(0, prefix.length());
    }

    // Lookup Q_CLASS_INFO for displayName and description
    for (int i = metaObject->classInfoOffset(); i < metaObject->classInfoCount(); ++i) {
        const QMetaClassInfo classInfo = metaObject->classInfo(i);
        const QString classInfoName = QString::fromLatin1(classInfo.name());

        if (classInfoName == id + QStringLiteral(".displayName")) {
            displayName = QString::fromLatin1(classInfo.value());
        } else if (classInfoName == id + QStringLiteral(".description")) {
            description = QString::fromLatin1(classInfo.value());
        }
    }

    // If displayName empty, use the method name as displayName
    if (displayName.isEmpty()) {
        displayName = id;
    }

    return !id.isEmpty() && !displayName.isEmpty() && !description.isEmpty();
}

class ObjectMethodTool : public BaseTool
{
public:
    virtual ~ObjectMethodTool() override;

    bool isValid() const
    {
        return m_method.isValid() && !m_id.isEmpty() && !m_displayName.isEmpty()
                && !m_description.isEmpty();
    }

    QString id() const override { return m_id; }
    QString displayName() const override { return m_displayName; }
    QString description() const override { return m_description; }
    QJsonObject parametersSchema() const override { return m_parametersSchema; }
    ToolSafety safety() const override { return ToolSafety::ReadOnly; }

    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &input) override;

private:
    explicit ObjectMethodTool(const QMetaMethod &method, AbstractToolObject *object,
                              QObject *parent = nullptr);

    void onObjectDestroyed(QObject *ptr);

private:
    QString m_id;
    QString m_displayName;
    QString m_description;
    QJsonObject m_parametersSchema;

    QMetaMethod m_method;
    AbstractToolObject *m_object = nullptr;

    friend class ObjectToolsAdapter;
};

// ObjectMethodTool
ObjectMethodTool::ObjectMethodTool(const QMetaMethod &method, AbstractToolObject *object,
                                   QObject *parent)
    : BaseTool(parent), m_method(method), m_object(object)
{
    connect(m_object, &QObject::destroyed, this, &ObjectMethodTool::onObjectDestroyed);

    if (!object->queryMethodInfo(method, m_id, m_displayName, m_description))
        return;

    // Infer parameters schema from the method's parameters
    const QList<QByteArray> parameterNames = m_method.parameterNames();
    QStringList allParameterNames;
    QJsonObject properties;
    for (int i = 0; i < parameterNames.size(); ++i) {
        QMetaType::Type paramType = QMetaType::Type(m_method.parameterType(i));
        QString parameterName = QString::fromLatin1(parameterNames.at(i));
        allParameterNames << parameterName;
        switch (paramType) {
        case QMetaType::Int:
            properties.insert(parameterName, QJsonObject { { "type", "integer" } });
            break;
        case QMetaType::Double:
            properties.insert(parameterName, QJsonObject { { "type", "number" } });
            break;
        case QMetaType::QString:
            properties.insert(parameterName, QJsonObject { { "type", "string" } });
            break;
        case QMetaType::Bool:
            properties.insert(parameterName, QJsonObject { { "type", "boolean" } });
            break;
        case QMetaType::QJsonObject:
            properties.insert(parameterName, QJsonObject { { "type", "object" } });
            break;
        case QMetaType::QJsonArray:
        case QMetaType::QStringList:
            properties.insert(parameterName, QJsonObject { { "type", "array" } });
            break;
        default:
            properties.insert(parameterName, QJsonObject { { "type", "unknown" } });
            break;
        }
    }
    m_parametersSchema.insert("type", QStringLiteral("object"));
    m_parametersSchema.insert("properties", properties);

    // QMetaMethod::invoke() doesnt work well with default parameters.
    // So, all parameters are required.
    m_parametersSchema.insert("required", QJsonArray::fromStringList(allParameterNames));
}

void ObjectMethodTool::onObjectDestroyed(QObject *ptr)
{
    if (ptr == m_object)
        m_object = nullptr;
}

ObjectMethodTool::~ObjectMethodTool() { }

static ToolResult invokeMethod(QObject *object, const QString &objectName,
                               const QMetaMethod &method, const QJsonObject &input)
{
    if (object == nullptr)
        return ToolResult::error(QStringLiteral("Tool object is unavailable."));

    // Prepare arguments for the method invocation
    QList<QVariant> params(10); // We cannot have more than 10 args anyway.
    QList<QGenericArgument> args;
    for (int i = 0; i < method.parameterCount(); ++i) {
        QString paramName = QString::fromLatin1(method.parameterNames().at(i));
        QVariant paramValue = input.value(paramName).toVariant();
        QMetaType paramType = method.parameterMetaType(i);

        if (paramValue.canConvert(paramType)) {
            paramValue.convert(paramType);
        } else {
            return ToolResult::error(
                    QStringLiteral("Cannot convert parameter %1 to required type").arg(paramName));
        }

        params[i] = paramValue;
        args.append(QGenericArgument(paramType.name(), params[i].data()));
    }

    // Invoke the method
    bool invoked = false;

    QVariant response;

    if (method.returnType() == QMetaType::Void) {
        invoked = method.invoke(object, Qt::BlockingQueuedConnection, args.value(0), args.value(1),
                                args.value(2), args.value(3), args.value(4), args.value(5),
                                args.value(6), args.value(7), args.value(8), args.value(9));
    } else {
        QMetaType returnType = method.returnMetaType();
        void *returnData = returnType.create();

        QGenericReturnArgument returnArg(returnType.name(), returnData);
        invoked = method.invoke(object, Qt::BlockingQueuedConnection, returnArg, args.value(0),
                                args.value(1), args.value(2), args.value(3), args.value(4),
                                args.value(5), args.value(6), args.value(7), args.value(8),
                                args.value(9));

        response = QVariant::fromMetaType(returnType, returnData);

        returnType.destroy(returnData);
    }

    if (!invoked) {
        return ToolResult::error(QStringLiteral("Failed to invoke method %1 on %2")
                                         .arg(QString::fromLatin1(method.name()), objectName));
    }

    if (response.userType() == QMetaType::QString)
        return ToolResult::text(response.toString());

    if (response.userType() == QMetaType::QStringList) {
        const QStringList list = response.toStringList();
        ToolResult r;
        for (const QString &s : list) {
            r.content.append(TextContent { s });
        }
        return r;
    }

    if (response.userType() == QMetaType::QJsonObject) {
        ToolResult r;

        const QJsonObject json = response.toJsonObject();
        const QString type = json.value("type").toString();
        if (QStringList({ "text", "image", "audio", "resource", "resource_link" }).contains(type)) {
            const ToolContent c = LLMQore::toolContentFromJson(response.toJsonObject());
            r.content.append(c);
        } else {
            r.structuredContent = json;
        }

        return r;
    }

    if (response.canConvert(QMetaType::fromType<QString>()))
        return ToolResult::text(response.toString());

    return ToolResult::text(QString("The tool was called successfully."));
}

QFuture<LLMQore::ToolResult> ObjectMethodTool::executeAsync(const QJsonObject &input)
{
    return QtConcurrent::run(invokeMethod, m_object, m_object->name(), m_method, input);
}

// ObjectToolsAdapter
ObjectToolsAdapter::ObjectToolsAdapter(AbstractToolObject *object)
    : QObject(ObjectToolsAdapterThread()), m_object(object), m_metaObject(object->metaObject())
{
}

ObjectToolsAdapter::~ObjectToolsAdapter() { }

QList<BaseTool *> ObjectToolsAdapter::registerTools(ToolRegistry *toolRegistry, MethodFilter filter)
{
    QList<BaseTool *> ret;
    if (toolRegistry == nullptr || m_object == nullptr)
        return ret;

    // Loop through the object's meta-methods and create ObjectMethodTool instances for each method
    // that matches the filter
    for (int i = m_metaObject->methodOffset(); i < m_metaObject->methodCount(); ++i) {
        QMetaMethod method = m_metaObject->method(i);

        // QMetaMethod::invoke() allows us to pass up to 10 args. So
        // parameter count cannot be more than 10.
        if (method.parameterCount() > 10)
            continue;

        bool useMethod = false;

        if ((int(filter) & PublicInvokable) && method.methodType() == QMetaMethod::Method
            && method.access() == QMetaMethod::Public)
            useMethod = true;

        if ((int(filter) & PublicSlot) && method.methodType() == QMetaMethod::Slot
            && method.access() == QMetaMethod::Public)
            useMethod = true;

        // Apply prefix filter if enabled
        const QString toolPrefix = m_object->toolPrefix();
        if (useMethod && !toolPrefix.isEmpty()) {
            const QString methodName = QString::fromLatin1(method.name());
            useMethod = methodName.startsWith(toolPrefix);
        }

        if (useMethod) {
            auto tool = new ObjectMethodTool(method, m_object, toolRegistry);
            if (!tool->isValid()) {
                delete tool;
            } else if (toolRegistry->tool(tool->id())) {
                delete tool; // Duplicate tool
            } else {
                toolRegistry->addTool(tool);
                ret << tool;
            }
        }
    }

    return ret;
}

ObjectToolsAdapter *ObjectToolsAdapter::create(AbstractToolObject *object, const QVariantMap &props)
{
    if (object == nullptr)
        return nullptr; // There has to be an object

    if (object->metaObject()->superClass() != &AbstractToolObject::staticMetaObject)
        return nullptr; // Only direct subclasses of AbstractToolObject allowed

    auto guard = qScopeGuard([object]() { object->deleteLater(); });

    if (qApp == nullptr)
        return nullptr; // There should be an event loop

    if (QThread::currentThread() != qApp->thread())
        return nullptr; // This function must be called in the main-thread.

    if (object->parent() != nullptr)
        return nullptr; // We need to be able to move the object to another thread.

    if (object->thread() != nullptr) {
        if (object->thread() != qApp->thread())
            return nullptr; // The object has to be on the main thread.
    }

    guard.dismiss();

    if (!props.isEmpty()) {
        auto it = props.constBegin();
        auto end = props.constEnd();
        while (it != end) {
            object->setProperty(qPrintable(it.key()), it.value());
            ++it;
        }
    }

    QThread *thread = ObjectToolsAdapterThread();
    QList<ObjectToolsAdapter *> adapters = thread->findChildren<ObjectToolsAdapter *>();
    ObjectToolsAdapter *adapter = nullptr;

    if (!adapters.isEmpty()) {
        auto it = std::find_if(
                adapters.begin(), adapters.end(),
                [object](ObjectToolsAdapter *ptr) -> bool { return ptr->object() == object; });
        if (it != adapters.end())
            adapter = *it;
    }

    if (adapter == nullptr) {
        adapter = new ObjectToolsAdapter(object);

        connect(object, &QObject::destroyed, adapter, &QObject::deleteLater);
        connect(thread, &QThread::finished, object, &QObject::deleteLater);
        connect(adapter, &QObject::destroyed, object, &QObject::deleteLater);

        object->moveToThread(thread);
    }

    return adapter;
}

} // namespace LLMQore
