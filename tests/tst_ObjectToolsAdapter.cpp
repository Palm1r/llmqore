// Copyright (C) 2026 Prashanth Udupa (prashanth@scrite.io)
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QSignalSpy>
#include <QThread>
#include <QtConcurrentRun>

#include <LLMQore/Log.hpp>
#include <LLMQore/ToolRegistry.hpp>
#include <LLMQore/ContentBlocks.hpp>
#include <LLMQore/ObjectToolsAdapter.hpp>

using namespace LLMQore;

class TestObject : public AbstractToolObject
{
    Q_OBJECT

public:
    static QString prefix;

    Q_PROPERTY(QString topic MEMBER m_topic)
    QString m_topic = "default";

    // Methods without tool_ prefix
    Q_INVOKABLE QString getTopic() const { return m_topic; }
    Q_INVOKABLE QString publicMethod(const QString &text) { return "Result: " + text; }
    Q_INVOKABLE int addNumbers(int a, int b) { return a + b; }
    Q_INVOKABLE bool returnBool() { return true; }

    // Methods with tool_ prefix
    Q_CLASSINFO("calculateSum.displayName", "Calculate Sum")
    Q_CLASSINFO("calculateSum.description", "Calculates the sum of three numbers")
    Q_INVOKABLE QJsonObject tool_calculateSum(int a, int b, int c)
    {
        return LLMQore::toolContentToJson(TextContent { QString::number(a, b, c) });
    }

    Q_CLASSINFO("generateImage.displayName", "Generate Image")
    Q_CLASSINFO("generateImage.description", "Generates an image with the given parameters")
    Q_INVOKABLE QJsonObject tool_generateImage(const QString &style, int width, int height)
    {
        Q_UNUSED(style);
        Q_UNUSED(width);
        Q_UNUSED(height);
        return LLMQore::toolContentToJson(
                ImageContent::fromBytes(QByteArray("fake_image_data"), "image/png"));
    }

    Q_CLASSINFO("generateAudio.displayName", "Generate Audio")
    Q_CLASSINFO("generateAudio.description", "Generates audio with the given parameters")
    Q_INVOKABLE QJsonObject tool_generateAudio(int duration, const QString &format)
    {
        Q_UNUSED(duration);
        Q_UNUSED(format);
        return LLMQore::toolContentToJson(
                AudioContent { QByteArray("fake_audio_data"), "audio/wav" });
    }

    Q_INVOKABLE QJsonObject tool_getResourceLink(const QString &filename, const QString &category)
    {
        Q_UNUSED(filename);
        Q_UNUSED(category);
        return LLMQore::toolContentToJson(ResourceLinkContent {
                "https://example.com/file.pdf", "Example PDF", "A sample PDF", "application/pdf" });
    }

public slots:
    QString publicSlot(const QString &text) { return "Slot: " + text; }

    QString tool_transformText(const QString &text, bool uppercase)
    {
        QString result = "Transformed: " + text;
        return uppercase ? result.toUpper() : result;
    }

private:
    Q_INVOKABLE QString privateMethod() { return "private"; }

protected:
    // AbstractToolObject interface
    QString toolPrefix() const { return prefix; }
    bool queryMethodInfo(const QMetaMethod &method, QString &id, QString &displayName,
                         QString &description)
    {
        AbstractToolObject::queryMethodInfo(method, id, displayName, description);
        if (description.isEmpty())
            description = QStringLiteral("Calls the method ") + id + QStringLiteral(" on ")
                    + this->name();

        return true;
    }
};

QString TestObject::prefix = QString();

class ObjectToolsAdapterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_ObjectToolsAdapter";
            static char *argv[] = { arg0 };
            m_app = new QCoreApplication(argc, argv);
        }
    }

    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }

    void cleanupAdapter(ObjectToolsAdapter *adapter)
    {
        if (!adapter)
            return;
        QSignalSpy spy(adapter, &QObject::destroyed);
        adapter->deleteLater();
        for (int i = 0; i < 10 && spy.count() == 0; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    }

    QCoreApplication *m_app = nullptr;
};

TEST_F(ObjectToolsAdapterTest, CreateAdapterWithObject)
{
    auto *adapter = ObjectToolsAdapter::create<TestObject>();
    ASSERT_NE(adapter, nullptr);

    // Adapter created successfully
    EXPECT_NE(adapter, nullptr);

    // Adapter has a valid object
    auto *obj = adapter->object();
    EXPECT_NE(obj, nullptr);
    EXPECT_TRUE(obj->isWidgetType() == false); // It's a QObject

    cleanupAdapter(adapter);
}

TEST_F(ObjectToolsAdapterTest, AdapterAndObjectLifecycleBinding)
{
    auto *adapter = ObjectToolsAdapter::create<TestObject>();
    ASSERT_NE(adapter, nullptr);
    auto *obj = adapter->object();
    ASSERT_NE(obj, nullptr);

    // Create a watcher to track when object is destroyed
    QSignalSpy objDestroyedSpy(obj, &QObject::destroyed);
    QSignalSpy adapterDestroyedSpy(adapter, &QObject::destroyed);

    // Delete adapter - object should also be deleted (via queued connection)
    delete adapter;

    // Process more events to let object deletion complete
    for (int i = 0; i < 10 && objDestroyedSpy.count() == 0; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    // Adapter should be destroyed
    EXPECT_GT(adapterDestroyedSpy.count(), 0);

    // Object should also be destroyed
    EXPECT_GT(objDestroyedSpy.count(), 0);
}

TEST_F(ObjectToolsAdapterTest, RegisterAllPublicAsTools)
{
    auto *adapter = ObjectToolsAdapter::create<TestObject>();
    ASSERT_NE(adapter, nullptr);

    ToolRegistry registry;
    auto tools = adapter->registerTools(&registry, ObjectToolsAdapter::AllPublic);

    // Should find all 10 public methods
    EXPECT_EQ(tools.size(), 10);

    // QString getTopic() const
    auto *getTopic = registry.tool("getTopic");
    ASSERT_NE(getTopic, nullptr);
    EXPECT_EQ(getTopic->id(), "getTopic");
    EXPECT_EQ(getTopic->displayName(), "getTopic");
    EXPECT_EQ(getTopic->description(), "Calls the method getTopic on TestObject");
    EXPECT_EQ(getTopic->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties", QJsonObject {} },
                            { "required", QJsonArray {} } }));
    EXPECT_EQ(getTopic->safety(), ToolSafety::ReadOnly);

    // QString publicMethod(const QString &text)
    auto *publicMethod = registry.tool("publicMethod");
    ASSERT_NE(publicMethod, nullptr);
    EXPECT_EQ(publicMethod->id(), "publicMethod");
    EXPECT_EQ(publicMethod->displayName(), "publicMethod");
    EXPECT_EQ(publicMethod->description(), "Calls the method publicMethod on TestObject");
    EXPECT_EQ(publicMethod->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "text", QJsonObject { { "type", "string" } } } } },
                            { "required", QJsonArray { "text" } } }));
    EXPECT_EQ(publicMethod->safety(), ToolSafety::ReadOnly);

    // int addNumbers(int a, int b)
    auto *addNumbers = registry.tool("addNumbers");
    ASSERT_NE(addNumbers, nullptr);
    EXPECT_EQ(addNumbers->id(), "addNumbers");
    EXPECT_EQ(addNumbers->displayName(), "addNumbers");
    EXPECT_EQ(addNumbers->description(), "Calls the method addNumbers on TestObject");
    EXPECT_EQ(addNumbers->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "a", QJsonObject { { "type", "integer" } } },
                                            { "b", QJsonObject { { "type", "integer" } } } } },
                            { "required", QJsonArray { "a", "b" } } }));
    EXPECT_EQ(addNumbers->safety(), ToolSafety::ReadOnly);

    // bool returnBool()
    auto *returnBool = registry.tool("returnBool");
    ASSERT_NE(returnBool, nullptr);
    EXPECT_EQ(returnBool->id(), "returnBool");
    EXPECT_EQ(returnBool->displayName(), "returnBool");
    EXPECT_EQ(returnBool->description(), "Calls the method returnBool on TestObject");
    EXPECT_EQ(returnBool->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties", QJsonObject {} },
                            { "required", QJsonArray {} } }));
    EXPECT_EQ(returnBool->safety(), ToolSafety::ReadOnly);

    // ToolResult tool_calculateSum(int a, int b, int c)
    auto *calculateSum = registry.tool("tool_calculateSum");
    ASSERT_NE(calculateSum, nullptr);
    EXPECT_EQ(calculateSum->id(), "tool_calculateSum");
    EXPECT_EQ(calculateSum->displayName(), "tool_calculateSum");
    EXPECT_EQ(calculateSum->description(), "Calls the method tool_calculateSum on TestObject");
    EXPECT_EQ(calculateSum->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "a", QJsonObject { { "type", "integer" } } },
                                            { "b", QJsonObject { { "type", "integer" } } },
                                            { "c", QJsonObject { { "type", "integer" } } } } },
                            { "required", QJsonArray { "a", "b", "c" } } }));
    EXPECT_EQ(calculateSum->safety(), ToolSafety::ReadOnly);

    // ImageContent tool_generateImage(const QString &style, int width, int height)
    auto *generateImage = registry.tool("tool_generateImage");
    ASSERT_NE(generateImage, nullptr);
    EXPECT_EQ(generateImage->id(), "tool_generateImage");
    EXPECT_EQ(generateImage->displayName(), "tool_generateImage");
    EXPECT_EQ(generateImage->description(), "Calls the method tool_generateImage on TestObject");
    EXPECT_EQ(generateImage->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "style", QJsonObject { { "type", "string" } } },
                                            { "width", QJsonObject { { "type", "integer" } } },
                                            { "height", QJsonObject { { "type", "integer" } } } } },
                            { "required", QJsonArray { "style", "width", "height" } } }));
    EXPECT_EQ(generateImage->safety(), ToolSafety::ReadOnly);

    // AudioContent tool_generateAudio(int duration, const QString &format)
    auto *generateAudio = registry.tool("tool_generateAudio");
    ASSERT_NE(generateAudio, nullptr);
    EXPECT_EQ(generateAudio->id(), "tool_generateAudio");
    EXPECT_EQ(generateAudio->displayName(), "tool_generateAudio");
    EXPECT_EQ(generateAudio->description(), "Calls the method tool_generateAudio on TestObject");
    EXPECT_EQ(generateAudio->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "duration", QJsonObject { { "type", "integer" } } },
                                            { "format", QJsonObject { { "type", "string" } } } } },
                            { "required", QJsonArray { "duration", "format" } } }));
    EXPECT_EQ(generateAudio->safety(), ToolSafety::ReadOnly);

    // ResourceLinkContent tool_getResourceLink(const QString &filename, const QString &category)
    auto *getResourceLink = registry.tool("tool_getResourceLink");
    ASSERT_NE(getResourceLink, nullptr);
    EXPECT_EQ(getResourceLink->id(), "tool_getResourceLink");
    EXPECT_EQ(getResourceLink->displayName(), "tool_getResourceLink");
    EXPECT_EQ(getResourceLink->description(),
              "Calls the method tool_getResourceLink on TestObject");
    EXPECT_EQ(
            getResourceLink->parametersSchema(),
            QJsonObject({ { "type", "object" },
                          { "properties",
                            QJsonObject { { "filename", QJsonObject { { "type", "string" } } },
                                          { "category", QJsonObject { { "type", "string" } } } } },
                          { "required", QJsonArray { "filename", "category" } } }));
    EXPECT_EQ(getResourceLink->safety(), ToolSafety::ReadOnly);

    // QString publicSlot(const QString &text)
    auto *publicSlot = registry.tool("publicSlot");
    ASSERT_NE(publicSlot, nullptr);
    EXPECT_EQ(publicSlot->id(), "publicSlot");
    EXPECT_EQ(publicSlot->displayName(), "publicSlot");
    EXPECT_EQ(publicSlot->description(), "Calls the method publicSlot on TestObject");
    EXPECT_EQ(publicSlot->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "text", QJsonObject { { "type", "string" } } } } },
                            { "required", QJsonArray { "text" } } }));
    EXPECT_EQ(publicSlot->safety(), ToolSafety::ReadOnly);

    // QString tool_transformText(const QString &text, bool uppercase)
    auto *transformText = registry.tool("tool_transformText");
    ASSERT_NE(transformText, nullptr);
    EXPECT_EQ(transformText->id(), "tool_transformText");
    EXPECT_EQ(transformText->displayName(), "tool_transformText");
    EXPECT_EQ(transformText->description(), "Calls the method tool_transformText on TestObject");
    EXPECT_EQ(transformText->parametersSchema(),
              QJsonObject(
                      { { "type", "object" },
                        { "properties",
                          QJsonObject { { "text", QJsonObject { { "type", "string" } } },
                                        { "uppercase", QJsonObject { { "type", "boolean" } } } } },
                        { "required", QJsonArray { "text", "uppercase" } } }));
    EXPECT_EQ(transformText->safety(), ToolSafety::ReadOnly);

    // Private methods should not be registered
    EXPECT_EQ(registry.tool("privateMethod"), nullptr);

    cleanupAdapter(adapter);
}

TEST_F(ObjectToolsAdapterTest, RegisterPublicMethodsWithPrefixAsTool)
{
    QScopedValueRollback<QString> rollback(TestObject::prefix, "tool_");

    auto *adapter = ObjectToolsAdapter::create<TestObject>();
    ASSERT_NE(adapter, nullptr);

    ToolRegistry registry;
    auto tools = adapter->registerTools(&registry, ObjectToolsAdapter::AllPublic);

    // Should find only 5 public methods with tool_ prefix
    EXPECT_EQ(tools.size(), 5);

    // Methods without tool_ prefix should not be found
    EXPECT_EQ(registry.tool("publicMethod"), nullptr);
    EXPECT_EQ(registry.tool("addNumbers"), nullptr);
    EXPECT_EQ(registry.tool("returnBool"), nullptr);

    // Methods with tool_ prefix should be found with prefix stripped
    auto *calculateSum = registry.tool("calculateSum");
    ASSERT_NE(calculateSum, nullptr);
    EXPECT_EQ(calculateSum->id(), "calculateSum");
    EXPECT_EQ(calculateSum->displayName(), "Calculate Sum");
    EXPECT_EQ(calculateSum->description(), "Calculates the sum of three numbers");
    EXPECT_EQ(calculateSum->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "a", QJsonObject { { "type", "integer" } } },
                                            { "b", QJsonObject { { "type", "integer" } } },
                                            { "c", QJsonObject { { "type", "integer" } } } } },
                            { "required", QJsonArray { "a", "b", "c" } } }));
    EXPECT_EQ(calculateSum->safety(), ToolSafety::ReadOnly);

    auto *generateImage = registry.tool("generateImage");
    ASSERT_NE(generateImage, nullptr);
    EXPECT_EQ(generateImage->id(), "generateImage");
    EXPECT_EQ(generateImage->displayName(), "Generate Image");
    EXPECT_EQ(generateImage->description(), "Generates an image with the given parameters");
    EXPECT_EQ(generateImage->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "style", QJsonObject { { "type", "string" } } },
                                            { "width", QJsonObject { { "type", "integer" } } },
                                            { "height", QJsonObject { { "type", "integer" } } } } },
                            { "required", QJsonArray { "style", "width", "height" } } }));
    EXPECT_EQ(generateImage->safety(), ToolSafety::ReadOnly);

    auto *generateAudio = registry.tool("generateAudio");
    ASSERT_NE(generateAudio, nullptr);
    EXPECT_EQ(generateAudio->id(), "generateAudio");
    EXPECT_EQ(generateAudio->displayName(), "Generate Audio");
    EXPECT_EQ(generateAudio->description(), "Generates audio with the given parameters");
    EXPECT_EQ(generateAudio->parametersSchema(),
              QJsonObject({ { "type", "object" },
                            { "properties",
                              QJsonObject { { "duration", QJsonObject { { "type", "integer" } } },
                                            { "format", QJsonObject { { "type", "string" } } } } },
                            { "required", QJsonArray { "duration", "format" } } }));
    EXPECT_EQ(generateAudio->safety(), ToolSafety::ReadOnly);

    auto *getResourceLink = registry.tool("getResourceLink");
    ASSERT_NE(getResourceLink, nullptr);
    EXPECT_EQ(getResourceLink->id(), "getResourceLink");
    EXPECT_EQ(getResourceLink->displayName(), "getResourceLink");
    EXPECT_EQ(getResourceLink->description(), "Calls the method getResourceLink on TestObject");
    EXPECT_EQ(
            getResourceLink->parametersSchema(),
            QJsonObject({ { "type", "object" },
                          { "properties",
                            QJsonObject { { "filename", QJsonObject { { "type", "string" } } },
                                          { "category", QJsonObject { { "type", "string" } } } } },
                          { "required", QJsonArray { "filename", "category" } } }));
    EXPECT_EQ(getResourceLink->safety(), ToolSafety::ReadOnly);

    auto *transformText = registry.tool("transformText");
    ASSERT_NE(transformText, nullptr);
    EXPECT_EQ(transformText->id(), "transformText");
    EXPECT_EQ(transformText->displayName(), "transformText");
    EXPECT_EQ(transformText->description(), "Calls the method transformText on TestObject");
    EXPECT_EQ(transformText->parametersSchema(),
              QJsonObject(
                      { { "type", "object" },
                        { "properties",
                          QJsonObject { { "text", QJsonObject { { "type", "string" } } },
                                        { "uppercase", QJsonObject { { "type", "boolean" } } } } },
                        { "required", QJsonArray { "text", "uppercase" } } }));
    EXPECT_EQ(transformText->safety(), ToolSafety::ReadOnly);

    // Methods with tool_ prefix should not be found with tool_ prefix in the registry
    EXPECT_EQ(registry.tool("tool_calculateSum"), nullptr);
    EXPECT_EQ(registry.tool("tool_generateImage"), nullptr);
    EXPECT_EQ(registry.tool("tool_generateAudio"), nullptr);
    EXPECT_EQ(registry.tool("tool_getResourceLink"), nullptr);
    EXPECT_EQ(registry.tool("tool_transformText"), nullptr);

    // Slots without tool_ prefix should not be found
    EXPECT_EQ(registry.tool("publicSlot"), nullptr);

    // Private methods should not be registered
    EXPECT_EQ(registry.tool("privateMethod"), nullptr);

    cleanupAdapter(adapter);
}

TEST_F(ObjectToolsAdapterTest, RegisterPublicSlots)
{
    auto *adapter = ObjectToolsAdapter::create<TestObject>();
    ASSERT_NE(adapter, nullptr);

    ToolRegistry registry;
    auto tools = adapter->registerTools(&registry, ObjectToolsAdapter::PublicSlot);

    // Should find all 2 public slots
    EXPECT_EQ(tools.size(), 2);

    // Slots should be found
    EXPECT_NE(registry.tool("publicSlot"), nullptr);
    EXPECT_NE(registry.tool("tool_transformText"), nullptr);

    // Methods without slot should not be found
    EXPECT_EQ(registry.tool("publicMethod"), nullptr);
    EXPECT_EQ(registry.tool("addNumbers"), nullptr);
    EXPECT_EQ(registry.tool("returnBool"), nullptr);
    EXPECT_EQ(registry.tool("tool_calculateSum"), nullptr);
    EXPECT_EQ(registry.tool("tool_generateImage"), nullptr);
    EXPECT_EQ(registry.tool("tool_generateAudio"), nullptr);
    EXPECT_EQ(registry.tool("tool_getResourceLink"), nullptr);

    // Private methods should not be registered
    EXPECT_EQ(registry.tool("privateMethod"), nullptr);

    cleanupAdapter(adapter);
}

TEST_F(ObjectToolsAdapterTest, PublicMethodsWithUnknownPrefixNotRegistered)
{
    QScopedValueRollback<QString> rollback(TestObject::prefix, "unknown_");

    auto *adapter = ObjectToolsAdapter::create<TestObject>();
    ASSERT_NE(adapter, nullptr);

    ToolRegistry registry;
    auto tools = adapter->registerTools(&registry, ObjectToolsAdapter::AllPublic);

    // Private methods should not be registered
    EXPECT_EQ(tools.size(), 0);

    cleanupAdapter(adapter);
}

TEST_F(ObjectToolsAdapterTest, CheckAdapterObjectThreadAffinity)
{
    // Adapter should be created in the main thread, but the object should be moved to a separate
    // thread
    auto *adapter = ObjectToolsAdapter::create<TestObject>();
    EXPECT_NE(adapter, nullptr);
    EXPECT_EQ(adapter->thread(), QCoreApplication::instance()->thread());

    auto *obj = adapter->object();
    EXPECT_NE(obj, nullptr);
    EXPECT_NE(obj->thread(), QCoreApplication::instance()->thread());

    cleanupAdapter(adapter);
}

inline bool operator==(const TextContent &a, const TextContent &b)
{
    return a.text == b.text;
}

inline bool operator==(const ImageContent &a, const ImageContent &b)
{
    return a.mimeType == b.mimeType && a.source == b.source;
}

inline bool operator==(const AudioContent &a, const AudioContent &b)
{
    return a.data == b.data && a.mimeType == b.mimeType;
}

inline bool operator==(const ResourceLinkContent &a, const ResourceLinkContent &b)
{
    return a.uri == b.uri && a.name == b.name && a.description == b.description
            && a.mimeType == b.mimeType;
}

TEST_F(ObjectToolsAdapterTest, CheckToolCalling)
{
    // Adapter should be created in the main thread, but the object should be moved to a separate
    // thread
    const QVariantMap props = { { "topic", "Test Topic" } };
    auto *adapter = ObjectToolsAdapter::create<TestObject>(props);
    ASSERT_NE(adapter, nullptr);

    auto *obj = adapter->object();
    ASSERT_NE(obj, nullptr);

    ToolRegistry registry;
    adapter->registerTools(&registry, ObjectToolsAdapter::AllPublic);

    auto testFn = [&](const QString &toolName, const QJsonObject &input) -> ToolContent {
        auto *tool = registry.tool(toolName);
        EXPECT_NE(tool, nullptr);

        auto future = tool->executeAsync(input);
        future.waitForFinished();

        auto result = future.result();
        EXPECT_EQ(result.content.size(), 1);

        return result.content.at(0);
    };

    TestObject testObject;

    // Test Q_INVOKABLE QString getTopic() const call
    auto getTopicResult = testFn("getTopic", QJsonObject());
    EXPECT_TRUE(std::get<TextContent>(getTopicResult)
                == TextContent { props.begin().value().toString() });

    // Test Q_INVOKABLE QString publicMethod(const QString &text) call
    auto publicMethodResult = testFn("publicMethod", QJsonObject({ { "text", "hello" } }));
    EXPECT_TRUE(std::get<TextContent>(publicMethodResult)
                == TextContent { testObject.publicMethod("hello") });

    // Test Q_INVOKABLE int addNumbers(int a, int b) call
    auto addNumbersResult = testFn("addNumbers", QJsonObject({ { "a", 10 }, { "b", 20 } }));
    EXPECT_TRUE(std::get<TextContent>(addNumbersResult)
                == TextContent { QString::number(testObject.addNumbers(10, 20)) });

    // Test Q_INVOKABLE bool returnBool() call
    auto returnBoolResult = testFn("returnBool", QJsonObject());
    EXPECT_TRUE(std::get<TextContent>(returnBoolResult)
                == TextContent { testObject.returnBool() ? "true" : "false" });

    // Test ToolResult tool_calculateSum(int a, int b, int c) call
    auto calculateSumResult =
            testFn("tool_calculateSum", QJsonObject({ { "a", 5 }, { "b", 10 }, { "c", 15 } }));
    auto calculateSumExpected = testObject.tool_calculateSum(5, 10, 15);
    EXPECT_TRUE(std::get<TextContent>(calculateSumResult)
                == std::get<TextContent>(LLMQore::toolContentFromJson(calculateSumExpected)));

    // Test ImageContent tool_generateImage(const QString &style, int width, int height) call
    auto generateImageResult =
            testFn("tool_generateImage",
                   QJsonObject({ { "style", "realistic" }, { "width", 512 }, { "height", 512 } }));
    EXPECT_TRUE(std::get<ImageContent>(generateImageResult)
                == std::get<ImageContent>(LLMQore::toolContentFromJson(
                        testObject.tool_generateImage("realistic", 512, 512))));

    // Test AudioContent tool_generateAudio(int duration, const QString &format) call
    auto generateAudioResult =
            testFn("tool_generateAudio", QJsonObject({ { "duration", 60 }, { "format", "wav" } }));
    EXPECT_TRUE(std::get<AudioContent>(generateAudioResult)
                == std::get<AudioContent>(
                        LLMQore::toolContentFromJson(testObject.tool_generateAudio(60, "wav"))));

    // Test ResourceLinkContent tool_getResourceLink(const QString &filename, const QString
    // &category) call
    auto getResourceLinkResult =
            testFn("tool_getResourceLink",
                   QJsonObject({ { "filename", "doc.pdf" }, { "category", "docs" } }));
    EXPECT_TRUE(std::get<ResourceLinkContent>(getResourceLinkResult)
                == std::get<ResourceLinkContent>(LLMQore::toolContentFromJson(
                        testObject.tool_getResourceLink("doc.pdf", "docs"))));

    // Test QString publicSlot(const QString &text) call
    auto publicSlotResult = testFn("publicSlot", QJsonObject({ { "text", "world" } }));
    EXPECT_TRUE(std::get<TextContent>(publicSlotResult)
                == TextContent { testObject.publicSlot("world") });

    // Test QString tool_transformText(const QString &text, bool uppercase) call
    auto transformTextResult = testFn("tool_transformText",
                                      QJsonObject({ { "text", "hello" }, { "uppercase", true } }));
    EXPECT_TRUE(std::get<TextContent>(transformTextResult)
                == TextContent { testObject.tool_transformText("hello", true) });

    cleanupAdapter(adapter);
}

TEST_F(ObjectToolsAdapterTest, CreateAdapterFromAnotherThreadFails)
{
    ObjectToolsAdapter *result = nullptr;

    QThread *thread = new QThread();
    QObject::connect(thread, &QThread::started, [&]() {
        result = ObjectToolsAdapter::create<TestObject>();
        thread->quit();
    });

    thread->start();
    thread->wait();
    thread->deleteLater();

    EXPECT_EQ(result, nullptr);
}

#include "tst_ObjectToolsAdapter.moc"
