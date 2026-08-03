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

using namespace LLMQore;
using namespace LLMQore::Mcp;

namespace {

template<typename T>
T resultOf(QFuture<T> future)
{
    return future.result();
}

} // namespace

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

    const ElicitResult result = resultOf(provider.elicit(params));

    EXPECT_EQ(seen.message, QStringLiteral("Your name?"));
    EXPECT_EQ(result.action, QString::fromLatin1(ElicitAction::Accept));
    EXPECT_EQ(result.content.value("name").toString(), QStringLiteral("qt"));
}

TEST(CallbackElicitationProviderTest, ANullCallbackCancels)
{
    CallbackElicitationProvider provider(nullptr);

    EXPECT_EQ(resultOf(provider.elicit({})).action, QString::fromLatin1(ElicitAction::Cancel))
        << "a host that installs the adapter without a callback must not hang the server";
}

TEST(DirectoryRootsProviderTest, TurnsPathsIntoFileUris)
{
    QTemporaryDir sandbox;
    ASSERT_TRUE(sandbox.isValid());

    DirectoryRootsProvider provider(QStringList{sandbox.path()});

    const QList<Root> roots = resultOf(provider.listRoots());
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
    EXPECT_EQ(changed.count(), 1);
    ASSERT_EQ(resultOf(provider.listRoots()).size(), 1);
    EXPECT_EQ(resultOf(provider.listRoots())[0].name, QStringLiteral("sandbox"));

    provider.addPath(sandbox.path());
    EXPECT_EQ(changed.count(), 1) << "the same directory twice is not a change";

    provider.removePath(sandbox.path());
    EXPECT_EQ(changed.count(), 2);
    EXPECT_TRUE(resultOf(provider.listRoots()).isEmpty());

    provider.removePath(sandbox.path());
    EXPECT_EQ(changed.count(), 2) << "removing what is not there is not a change";
}

TEST(DirectoryRootsProviderTest, SetPathsIsQuietWhenNothingMoved)
{
    QTemporaryDir sandbox;
    ASSERT_TRUE(sandbox.isValid());

    DirectoryRootsProvider provider(QStringList{sandbox.path()});
    QSignalSpy changed(&provider, &BaseRootsProvider::listChanged);

    provider.setPaths(QStringList{sandbox.path()});
    EXPECT_EQ(changed.count(), 0);

    provider.setPaths({});
    EXPECT_EQ(changed.count(), 1);
}

TEST(StaticResourceProviderTest, ListsInInsertionOrderAndReadsBack)
{
    StaticResourceProvider provider;
    provider.addText(QStringLiteral("mem://a"), QStringLiteral("alpha"));
    provider.addText(QStringLiteral("mem://b"), QStringLiteral("beta"), QStringLiteral("Beta"));

    const QList<ResourceInfo> resources = resultOf(provider.listResources());
    ASSERT_EQ(resources.size(), 2);
    EXPECT_EQ(resources[0].uri, QStringLiteral("mem://a"));
    EXPECT_EQ(resources[0].name, QStringLiteral("mem://a"));
    EXPECT_EQ(resources[1].name, QStringLiteral("Beta"));

    const ResourceContents contents = resultOf(provider.readResource(QStringLiteral("mem://b")));
    EXPECT_EQ(contents.text, QStringLiteral("beta"));
    EXPECT_EQ(contents.mimeType, QStringLiteral("text/plain"));
    EXPECT_TRUE(contents.blob.isEmpty());
}

TEST(StaticResourceProviderTest, BlobsKeepTheirBytes)
{
    StaticResourceProvider provider;
    provider.addBlob(QStringLiteral("mem://png"), QByteArray("\x89PNG", 4));

    const ResourceContents contents = resultOf(provider.readResource(QStringLiteral("mem://png")));
    EXPECT_EQ(contents.blob, QByteArray("\x89PNG", 4));
    EXPECT_TRUE(contents.text.isEmpty());
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

TEST(StaticResourceProviderTest, ReplacingAResourceAnnouncesAnUpdate)
{
    StaticResourceProvider provider;
    QSignalSpy listChanged(&provider, &BaseResourceProvider::listChanged);
    QSignalSpy updated(&provider, &BaseResourceProvider::resourceUpdated);

    provider.addText(QStringLiteral("mem://a"), QStringLiteral("one"));
    provider.addText(QStringLiteral("mem://a"), QStringLiteral("two"));

    EXPECT_EQ(provider.count(), 1);
    EXPECT_EQ(updated.count(), 2);
    EXPECT_EQ(updated.last().at(0).toString(), QStringLiteral("mem://a"));
    EXPECT_EQ(resultOf(provider.readResource(QStringLiteral("mem://a"))).text, QStringLiteral("two"));

    provider.remove(QStringLiteral("mem://a"));
    EXPECT_FALSE(provider.contains(QStringLiteral("mem://a")));
    EXPECT_EQ(listChanged.count(), 3);
}

TEST(StaticResourceProviderTest, InheritsTheOptionalHalfOfTheSeam)
{
    StaticResourceProvider provider;

    EXPECT_TRUE(resultOf(provider.listResourceTemplates()).isEmpty());
    EXPECT_FALSE(provider.supportsSubscription());
}
