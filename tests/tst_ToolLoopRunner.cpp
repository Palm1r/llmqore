// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QFuture>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSignalSpy>

#include <LLMQore/BaseClient.hpp>
#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/ToolLoopRunner.hpp>

using namespace LLMQore;

namespace {

class FakeClient : public BaseClient
{
public:
    using BaseClient::BaseClient;

    RequestID begin(const QJsonObject &payload = QJsonObject{{"seed", 1}})
    {
        const RequestID id = createRequest();
        storeRequestContext(id, QUrl("http://fake.local"), payload);
        return id;
    }

    void finish(const RequestID &id) { completeRequest(id); }

    QList<QJsonObject> continued;
    QStringList continuedIds;
    bool replayAvailable = true;
    int replayCalls = 0;

    void continueRequest(const RequestID &id, const QJsonObject &payload) override
    {
        continuedIds << id;
        continued << payload;
    }

    RequestID sendMessage(const QJsonObject &, const QString &, RequestMode) override
    {
        return {};
    }
    RequestID ask(const QString &, RequestMode) override { return {}; }
    QFuture<QList<QString>> listModels() override { return {}; }

protected:
    ToolSchemaFormat toolSchemaFormat() const override { return ToolSchemaFormat::OpenAI; }
    void processData(const RequestID &, const QByteArray &) override {}
    void processBufferedResponse(const RequestID &, const QByteArray &) override {}
    QNetworkRequest prepareNetworkRequest(const QUrl &url) const override
    {
        return QNetworkRequest(url);
    }
    BaseMessage *messageForRequest(const RequestID &) const override
    {
        return replayAvailable ? &m_message : nullptr;
    }
    void cleanupDerivedData(const RequestID &) override {}
    QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload, BaseMessage *, const QHash<QString, ToolResult> &) override
    {
        QJsonObject payload = originalPayload;
        payload["round"] = ++replayCalls;
        return payload;
    }

private:
    mutable BaseMessage m_message;
};

} // namespace

TEST(ToolLoopRunnerTest, ReplayContinuationFlowsThroughContinueRequest)
{
    FakeClient client;
    const RequestID id = client.begin();

    client.toolLoop()->handleToolsCompleted(id, {});

    ASSERT_EQ(client.continued.size(), 1);
    EXPECT_EQ(client.continuedIds.first(), id);
    EXPECT_EQ(client.continued.first().value("seed").toInt(), 1);
    EXPECT_EQ(client.continued.first().value("round").toInt(), 1);
    EXPECT_EQ(client.toolLoop()->rounds(id), 1);
}

TEST(ToolLoopRunnerTest, RoundLimitAbortsRequest)
{
    FakeClient client;
    client.toolLoop()->setMaxRounds(2);
    const RequestID id = client.begin();

    QSignalSpy failedSpy(&client, &BaseClient::requestFailed);

    client.toolLoop()->handleToolsCompleted(id, {});
    client.toolLoop()->handleToolsCompleted(id, {});
    client.toolLoop()->handleToolsCompleted(id, {});

    EXPECT_EQ(client.continued.size(), 2);
    ASSERT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(failedSpy.first().at(0).toString(), id);
    EXPECT_EQ(failedSpy.first().at(1).toString(), "Tool continuation limit reached");
    EXPECT_EQ(client.toolLoop()->rounds(id), 0);
}

TEST(ToolLoopRunnerTest, MissingReplayDataAbortsRequest)
{
    FakeClient client;
    client.replayAvailable = false;
    const RequestID id = client.begin();

    QSignalSpy failedSpy(&client, &BaseClient::requestFailed);

    client.toolLoop()->handleToolsCompleted(id, {});

    EXPECT_TRUE(client.continued.isEmpty());
    ASSERT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(failedSpy.first().at(1).toString(), "Missing data for tool continuation");
}

TEST(ToolLoopRunnerTest, ConcurrentRequestsTrackedIndependently)
{
    FakeClient client;
    client.toolLoop()->setMaxRounds(2);
    const RequestID first = client.begin();
    const RequestID second = client.begin();

    QSignalSpy failedSpy(&client, &BaseClient::requestFailed);

    client.toolLoop()->handleToolsCompleted(first, {});
    client.toolLoop()->handleToolsCompleted(first, {});
    client.toolLoop()->handleToolsCompleted(second, {});
    client.toolLoop()->handleToolsCompleted(first, {});
    client.toolLoop()->handleToolsCompleted(second, {});

    EXPECT_EQ(client.continued.size(), 4);
    ASSERT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(failedSpy.first().at(0).toString(), first);
    EXPECT_EQ(client.toolLoop()->rounds(second), 2);
}

TEST(ToolLoopRunnerTest, LoopStateClearedOnRequestFinalized)
{
    FakeClient client;
    const RequestID id = client.begin();

    client.toolLoop()->handleToolsCompleted(id, {});
    EXPECT_EQ(client.toolLoop()->rounds(id), 1);

    client.finish(id);
    EXPECT_EQ(client.toolLoop()->rounds(id), 0);
}

TEST(ToolLoopRunnerTest, LoopStateClearedOnCancel)
{
    FakeClient client;
    const RequestID id = client.begin();

    client.toolLoop()->handleToolsCompleted(id, {});
    EXPECT_EQ(client.toolLoop()->rounds(id), 1);

    client.cancelRequest(id);
    EXPECT_EQ(client.toolLoop()->rounds(id), 0);
}

TEST(ToolLoopRunnerTest, MaxToolContinuationsForwardsToRunner)
{
    FakeClient client;

    EXPECT_EQ(client.maxToolContinuations(), ToolLoopRunner::kDefaultMaxRounds);

    client.setMaxToolContinuations(5);
    EXPECT_EQ(client.maxToolContinuations(), 5);
    EXPECT_EQ(client.toolLoop()->maxRounds(), 5);

    client.setMaxToolContinuations(0);
    EXPECT_EQ(client.maxToolContinuations(), 1);
}
