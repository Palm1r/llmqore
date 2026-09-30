// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include <LLMQore/CallbackElicitationProvider.hpp>
#include <LLMQore/DirectoryRootsProvider.hpp>
#include <LLMQore/RpcExceptions.hpp>
#include <LLMQore/StaticResourceProvider.hpp>

#include "TestHelpers.hpp"

using namespace LLMQore;
using namespace LLMQore::Mcp;

TEST(CallbackElicitationProviderTest, HandsTheRequestToTheCallback)
{
    ElicitRequestParams seen;
    CallbackElicitationProvider provider([&seen](const ElicitRequestParams &params) {
        seen = params;
        return ElicitResult{QString::fromLatin1(ElicitAction::Accept), QJsonObject{{"name", "qt"}}};
    });

    ElicitRequestParams params;
    params.message = QStringLiteral("Your name?");
    params.requestedSchema = QJsonObject{{"type", "object"}};

    const ElicitResult result = waitForFuture(provider.elicit(params));

    EXPECT_EQ(seen.message, QStringLiteral("Your name?"));
    EXPECT_EQ(result.action, QString::fromLatin1(ElicitAction::Accept));
    EXPECT_EQ(result.content.value("name").toString(), QStringLiteral("qt"));
}

TEST(CallbackElicitationProviderTest, ANullCallbackCancels)
{
    CallbackElicitationProvider provider(nullptr);

    EXPECT_EQ(waitForFuture(provider.elicit({})).action, QString::fromLatin1(ElicitAction::Cancel))
        << "a host that installs the adapter without a callback must not hang the server";
}

TEST(DirectoryRootsProviderTest, TurnsPathsIntoFileUris)
{
    QTemporaryDir sandbox;
    ASSERT_TRUE(sandbox.isValid());

    DirectoryRootsProvider provider(QStringList{sandbox.path()});

    const QList<Root> roots = waitForFuture(provider.listRoots());
    ASSERT_EQ(roots.size(), 1);
    EXPECT_TRUE(roots[0].uri.startsWith(QStringLiteral("file://")));
    EXPECT_EQ(QUrl(roots[0].uri).toLocalFile(), QDir(sandbox.path()).absolutePath());
    EXPECT_FALSE(roots[0].name.isEmpty());
}

TEST(DirectoryRootsProviderTest, AddAndRemoveAnnounceTheChange)
{
    QTemporaryDir sandbox;
    ASSERT_TRUE(sandbox.isValid());

    DirectoryRootsProvider provider;
    QSignalSpy changed(&provider, &BaseRootsProvider::listChanged);

    provider.addPath(sandbox.path(), QStringLiteral("sandbox"));
    EXPECT_EQ(changed.size(), 1);
    ASSERT_EQ(waitForFuture(provider.listRoots()).size(), 1);
    EXPECT_EQ(waitForFuture(provider.listRoots())[0].name, QStringLiteral("sandbox"));

    provider.addPath(sandbox.path());
    EXPECT_EQ(changed.size(), 1) << "the same directory twice is not a change";

    provider.removePath(sandbox.path());
    EXPECT_EQ(changed.size(), 2);
    EXPECT_TRUE(waitForFuture(provider.listRoots()).isEmpty());

    provider.removePath(sandbox.path());
    EXPECT_EQ(changed.size(), 2) << "removing what is not there is not a change";
}

TEST(DirectoryRootsProviderTest, SetPathsIsQuietWhenNothingMoved)
{
    QTemporaryDir sandbox;
    ASSERT_TRUE(sandbox.isValid());

    DirectoryRootsProvider provider(QStringList{sandbox.path()});
    QSignalSpy changed(&provider, &BaseRootsProvider::listChanged);

    provider.setPaths(QStringList{sandbox.path()});
    EXPECT_EQ(changed.size(), 0);

    provider.setPaths({});
    EXPECT_EQ(changed.size(), 1);
}

TEST(StaticResourceProviderTest, ListsInInsertionOrderAndReadsBack)
{
    StaticResourceProvider provider;
    provider.addText(QStringLiteral("mem://a"), QStringLiteral("alpha"));
    provider.addText(QStringLiteral("mem://b"), QStringLiteral("beta"), QStringLiteral("Beta"));

    const QList<ResourceInfo> resources = waitForFuture(provider.listResources());
    ASSERT_EQ(resources.size(), 2);
    EXPECT_EQ(resources[0].uri, QStringLiteral("mem://a"));
    EXPECT_EQ(resources[0].name, QStringLiteral("mem://a"));
    EXPECT_EQ(resources[1].name, QStringLiteral("Beta"));

    const ResourceContents contents
        = waitForFuture(provider.readResource(QStringLiteral("mem://b")));
    EXPECT_EQ(contents.text, QStringLiteral("beta"));
    EXPECT_EQ(contents.mimeType, QStringLiteral("text/plain"));
    EXPECT_TRUE(contents.blob.isEmpty());
}

TEST(StaticResourceProviderTest, BlobsKeepTheirBytes)
{
    StaticResourceProvider provider;
    provider.addBlob(QStringLiteral("mem://png"), QByteArray("\x89PNG", 4));

    const ResourceContents contents
        = waitForFuture(provider.readResource(QStringLiteral("mem://png")));
    EXPECT_EQ(contents.blob, QByteArray("\x89PNG", 4));
    EXPECT_TRUE(contents.text.isEmpty());
}

TEST(StaticResourceProviderTest, BlobsDefaultToOctetStream)
{
    StaticResourceProvider provider;
    provider.addBlob(QStringLiteral("mem://bin"), QByteArray("\x01", 1));

    const QList<ResourceInfo> listed = waitForFuture(provider.listResources());
    ASSERT_EQ(listed.size(), 1);
    EXPECT_EQ(listed.first().mimeType, QStringLiteral("application/octet-stream"));
    EXPECT_EQ(
        waitForFuture(provider.readResource(QStringLiteral("mem://bin"))).mimeType,
        QStringLiteral("application/octet-stream"));
}

TEST(StaticResourceProviderTest, ClearEmptiesTheProviderInOneChange)
{
    StaticResourceProvider provider;
    provider.addText(QStringLiteral("mem://a"), QStringLiteral("1"));
    provider.addBlob(QStringLiteral("mem://b"), QByteArray("\x01", 1));
    QSignalSpy listChanged(&provider, &BaseResourceProvider::listChanged);

    provider.clear();

    EXPECT_TRUE(provider.isEmpty());
    EXPECT_EQ(provider.size(), 0);
    EXPECT_EQ(listChanged.size(), 1);
    EXPECT_TRUE(waitForFuture(provider.listResources()).isEmpty());

    provider.clear();
    EXPECT_EQ(listChanged.size(), 1) << "clearing nothing is not a change";
}

TEST(StaticResourceProviderTest, AnUnknownUriFailsRatherThanReturningEmptyContents)
{
    StaticResourceProvider provider;

    auto future = provider.readResource(QStringLiteral("mem://missing"));
    ASSERT_TRUE(future.isFinished());

    bool threw = false;
    try {
        future.result();
    } catch (const Rpc::JsonRpcException &e) {
        threw = true;
        EXPECT_TRUE(QString::fromUtf8(e.what()).contains(QStringLiteral("mem://missing")));
    }
    EXPECT_TRUE(threw) << "an empty ResourceContents would look like an empty file";
}

TEST(StaticResourceProviderTest, ReplacingAResourceKeepsOneEntryWithTheNewContents)
{
    StaticResourceProvider provider;

    provider.addText(QStringLiteral("mem://a"), QStringLiteral("one"));
    provider.addText(QStringLiteral("mem://a"), QStringLiteral("two"));

    EXPECT_EQ(provider.size(), 1);
    EXPECT_EQ(
        waitForFuture(provider.readResource(QStringLiteral("mem://a"))).text,
        QStringLiteral("two"));

    provider.remove(QStringLiteral("mem://a"));
    EXPECT_FALSE(provider.contains(QStringLiteral("mem://a")));
}

TEST(StaticResourceProviderTest, AnnouncesAListChangeOnlyWhenTheListChanges)
{
    StaticResourceProvider provider;
    QSignalSpy listChanged(&provider, &BaseResourceProvider::listChanged);

    provider.addText(QStringLiteral("mem://a"), QStringLiteral("one"));
    EXPECT_EQ(listChanged.size(), 1) << "a new resource is a new entry in resources/list";

    provider.addText(QStringLiteral("mem://a"), QStringLiteral("two"));
    EXPECT_EQ(listChanged.size(), 1)
        << "new contents under the same entry leave resources/list as it was";

    provider.addText(QStringLiteral("mem://a"), QStringLiteral("three"), QStringLiteral("Renamed"));
    EXPECT_EQ(listChanged.size(), 2) << "a new name is a change of the listed entry";

    provider.remove(QStringLiteral("mem://a"));
    EXPECT_EQ(listChanged.size(), 3);
}

TEST(StaticResourceProviderTest, NeverAnnouncesUpdatesNobodyCanSubscribeTo)
{
    StaticResourceProvider provider;
    QSignalSpy updated(&provider, &BaseResourceProvider::resourceUpdated);

    provider.addText(QStringLiteral("mem://a"), QStringLiteral("one"));
    provider.addText(QStringLiteral("mem://a"), QStringLiteral("two"));
    provider.remove(QStringLiteral("mem://a"));

    EXPECT_TRUE(updated.isEmpty())
        << "resources/updated goes to subscribers only, and this provider takes no subscriptions";
}

TEST(StaticResourceProviderTest, InheritsTheOptionalHalfOfTheSeam)
{
    StaticResourceProvider provider;

    EXPECT_TRUE(waitForFuture(provider.listResourceTemplates()).isEmpty());
    EXPECT_FALSE(provider.supportsSubscription());
}
