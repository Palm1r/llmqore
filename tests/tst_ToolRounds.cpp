// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QSignalSpy>

#include <LLMQore/BaseTool.hpp>
#include <LLMQore/ClaudeClient.hpp>
#include <LLMQore/Conversation.hpp>
#include <LLMQore/GoogleAIClient.hpp>
#include <LLMQore/OllamaClient.hpp>
#include <LLMQore/OpenAIClient.hpp>
#include <LLMQore/OpenAIResponsesClient.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "FakeHttpTransport.hpp"

using namespace LLMQore;
using LLMQoreTest::FakeHttpTransport;

namespace {

const int kRegisterRoundMetaTypes = []() {
    qRegisterMetaType<TokenUsage>("LLMQore::TokenUsage");
    qRegisterMetaType<CompletionInfo>("LLMQore::CompletionInfo");
    qRegisterMetaType<RequestID>("LLMQore::RequestID");
    return 0;
}();

class CountingTool : public BaseTool
{
    Q_OBJECT
public:
    using BaseTool::BaseTool;

    QString id() const override { return QStringLiteral("echo"); }
    QString displayName() const override { return QStringLiteral("Echo"); }
    QString description() const override { return QStringLiteral("Counts its calls"); }
    QJsonObject parametersSchema() const override { return QJsonObject{{"type", "object"}}; }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override
    {
        ++calls;
        QPromise<ToolResult> promise;
        QFuture<ToolResult> future = promise.future();
        promise.start();
        promise.addResult(ToolResult::text(QString::number(calls)));
        promise.finish();
        return future;
    }

    int calls = 0;
};

// One assistant turn that calls `echo` with the given tool-call id.
QByteArray toolCallTurn(const QByteArray &toolId)
{
    return "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"" + toolId
           + "\",\"function\":{\"name\":\"echo\",\"arguments\":\"{}\"}}]}}]}\n\n"
             "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
             "data: [DONE]\n\n";
}

QByteArray twoToolCallTurn(const QByteArray &firstName, const QByteArray &secondName)
{
    return "data: {\"choices\":[{\"delta\":{\"tool_calls\":["
           "{\"index\":0,\"id\":\"call_1\",\"function\":{\"name\":\""
           + firstName
           + "\",\"arguments\":\"{}\"}},"
             "{\"index\":1,\"id\":\"call_2\",\"function\":{\"name\":\""
           + secondName
           + "\",\"arguments\":\"{}\"}}]}}]}\n\n"
             "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
             "data: [DONE]\n\n";
}

QStringList toolCallIdsOf(const QJsonObject &payload)
{
    QStringList ids;
    const QJsonArray messages = payload.value("messages").toArray();
    for (const QJsonValue &m : messages) {
        const QJsonObject o = m.toObject();
        if (o.value("role").toString() == QLatin1String("tool"))
            ids << o.value("tool_call_id").toString();
    }
    return ids;
}

QByteArray finalTurn(const QByteArray &text)
{
    return "data: {\"choices\":[{\"delta\":{\"content\":\"" + text
           + "\"}}]}\n\n"
             "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
             "data: [DONE]\n\n";
}

// Tool completion reaches the loop through a queued QFutureWatcher signal, so
// an outcome that sends no further request has to be waited for explicitly.
void pump(int rounds = 20)
{
    for (int i = 0; i < rounds; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

TEST(ToolRounds, RepeatedToolCallIdInTheNextRoundStillExecutes)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    auto *tool = new CountingTool(&client);
    client.tools()->addTool(tool);

    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    const RequestID id = client.ask(QStringLiteral("go"));
    ASSERT_EQ(transport.streamCount(), 1);

    // The model calls the same id twice in a row -- exactly what a small local
    // model does. A per-request dedup swallows the second call and the loop hangs.
    transport.lastStream()->sendAll(toolCallTurn("call_same"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2)) << "round 1 did not continue";
    EXPECT_EQ(client.toolRounds(id), 1);

    transport.lastStream()->sendAll(toolCallTurn("call_same"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 3))
        << "round 2 reused the tool-call id and was swallowed by the dedup";
    EXPECT_EQ(client.toolRounds(id), 2);
    EXPECT_EQ(tool->calls, 2) << "the repeated id must run again in the new round";

    transport.lastStream()->sendAll(finalTurn("done"));
    ASSERT_EQ(completed.count(), 1);
    EXPECT_EQ(client.toolRounds(id), 0) << "the ledger dies with the request";
}

TEST(ToolRounds, ResultsHandedToTheLoopAreScopedToTheClosingRound)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    client.ask(QStringLiteral("go"));
    transport.lastStream()->sendAll(toolCallTurn("call_a"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));

    transport.lastStream()->sendAll(toolCallTurn("call_b"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 3));

    const QJsonArray messages = transport.streamRequest(2).payload().value("messages").toArray();
    int toolMessages = 0;
    for (const QJsonValue &m : messages) {
        if (m.toObject().value("role").toString() == QLatin1String("tool"))
            ++toolMessages;
    }
    EXPECT_EQ(toolMessages, 2) << "history keeps both rounds";

    const QJsonObject lastToolMessage = messages.last().toObject();
    EXPECT_EQ(lastToolMessage.value("tool_call_id").toString(), QStringLiteral("call_b"))
        << "the closing round must contribute its own result, not a replay of round 1";
}

TEST(ToolRounds, ParallelRequestsSharingAToolCallIdDoNotCollide)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    auto *tool = new CountingTool(&client);
    client.tools()->addTool(tool);

    const RequestID first = client.ask(QStringLiteral("one"));
    const RequestID second = client.ask(QStringLiteral("two"));
    ASSERT_EQ(transport.streamCount(), 2);
    ASSERT_NE(first, second);

    // Both turns use "toolu_1" -- providers mint ids per request, not globally.
    transport.streamAt(0)->sendAll(toolCallTurn("toolu_1"));
    transport.streamAt(1)->sendAll(toolCallTurn("toolu_1"));

    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 4))
        << "one of the two requests never continued";
    EXPECT_EQ(tool->calls, 2) << "each request must run its own tool call";
    EXPECT_EQ(client.toolRounds(first), 1);
    EXPECT_EQ(client.toolRounds(second), 1);
}

TEST(ToolRounds, RoundLimitAbortsTheRequest)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));
    client.setMaxToolContinuations(2);

    QSignalSpy failed(&client, &BaseClient::requestFailed);

    const RequestID id = client.ask(QStringLiteral("go"));

    transport.lastStream()->sendAll(toolCallTurn("call_1"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));
    transport.lastStream()->sendAll(toolCallTurn("call_2"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 3));

    transport.lastStream()->sendAll(toolCallTurn("call_3"));
    pump();

    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed.first().at(0).toString(), id);
    EXPECT_EQ(failed.first().at(1).toString(), QStringLiteral("Tool continuation limit reached"));
    EXPECT_EQ(transport.streamCount(), 3) << "no fourth turn may be sent";
    EXPECT_EQ(client.toolRounds(id), 0);
}

TEST(ToolRounds, TwoLoopsAdvanceIndependently)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));
    client.setMaxToolContinuations(2);

    QSignalSpy failed(&client, &BaseClient::requestFailed);

    const RequestID first = client.ask(QStringLiteral("one"));
    const RequestID second = client.ask(QStringLiteral("two"));
    ASSERT_EQ(transport.streamCount(), 2);

    auto advance = [&](int streamIndex, const QByteArray &toolId, int expectedStreams) {
        transport.streamAt(streamIndex)->sendAll(toolCallTurn(toolId));
        ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, expectedStreams));
    };

    advance(0, "a1", 3);
    advance(1, "b1", 4);
    EXPECT_EQ(client.toolRounds(first), 1);
    EXPECT_EQ(client.toolRounds(second), 1);

    advance(2, "a2", 5);
    EXPECT_EQ(client.toolRounds(first), 2);
    EXPECT_EQ(client.toolRounds(second), 1) << "the second loop must not advance with the first";

    // The first loop hits its limit; the second is still free to continue.
    transport.streamAt(4)->sendAll(toolCallTurn("a3"));
    pump();
    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed.first().at(0).toString(), first);

    advance(3, "b2", 6);
    EXPECT_EQ(client.toolRounds(second), 2);
}

TEST(ToolRounds, MaxToolContinuationsIsClampedToAtLeastOne)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);

    EXPECT_EQ(client.maxToolContinuations(), BaseClient::kDefaultMaxToolRounds);

    client.setMaxToolContinuations(5);
    EXPECT_EQ(client.maxToolContinuations(), 5);

    client.setMaxToolContinuations(0);
    EXPECT_EQ(client.maxToolContinuations(), 1);
}

TEST(ToolRounds, ASecondRequestStartsItsOwnRoundCount)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    const RequestID first = client.ask(QStringLiteral("one"));
    transport.lastStream()->sendAll(toolCallTurn("call_1"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));
    transport.lastStream()->sendAll(toolCallTurn("call_2"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 3));
    EXPECT_EQ(client.toolRounds(first), 2);

    transport.lastStream()->sendAll(finalTurn("done"));
    EXPECT_EQ(client.toolRounds(first), 0);

    const RequestID second = client.ask(QStringLiteral("two"));
    transport.lastStream()->sendAll(toolCallTurn("call_1"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 5));
    EXPECT_EQ(client.toolRounds(second), 1)
        << "the round count is the request's own, not a counter carried over by ToolsManager";
}

TEST(ToolRounds, AnUnknownToolBesideAValidOneStillClosesOneRound)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    const RequestID id = client.ask(QStringLiteral("go"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(twoToolCallTurn("no_such_tool", "echo"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2)) << "the round never continued";
    pump();

    EXPECT_EQ(transport.streamCount(), 2) << "one model turn must produce exactly one continuation";
    EXPECT_EQ(client.toolRounds(id), 1);

    const QStringList ids = toolCallIdsOf(transport.streamRequest(1).payload());
    EXPECT_EQ(ids, (QStringList{"call_1", "call_2"}))
        << "both calls of the round must be answered in the same continuation";
}

TEST(ToolRounds, TwoValidToolsCloseOneRound)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    auto *tool = new CountingTool(&client);
    client.tools()->addTool(tool);

    const RequestID id = client.ask(QStringLiteral("go"));
    transport.lastStream()->sendAll(twoToolCallTurn("echo", "echo"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));
    pump();

    EXPECT_EQ(transport.streamCount(), 2);
    EXPECT_EQ(client.toolRounds(id), 1);
    EXPECT_EQ(tool->calls, 2);
    EXPECT_EQ(toolCallIdsOf(transport.streamRequest(1).payload()), (QStringList{"call_1", "call_2"}));
}

TEST(ToolRounds, CancelClearsTheLedger)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    const RequestID id = client.ask(QStringLiteral("go"));
    transport.lastStream()->sendAll(toolCallTurn("call_1"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));
    EXPECT_EQ(client.toolRounds(id), 1);

    client.cancelRequest(id);
    EXPECT_EQ(client.toolRounds(id), 0);
}

#include "tst_ToolRounds.moc"

TEST(ToolRounds, ContinuationDeltaWithEmptyIdDoesNotStartAPhantomCall)
{
    LLMQoreTest::FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    auto *tool = new CountingTool(&client);
    client.tools()->addTool(tool);

    QSignalSpy completed(&client, &BaseClient::requestCompleted);

    const RequestID id = client.ask(QStringLiteral("go"));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
        "\"function\":{\"name\":\"echo\",\"arguments\":\"\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"\","
        "\"function\":{\"name\":\"\",\"arguments\":\"{}\"}}]}}]}\n\n"
        "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
        "data: [DONE]\n\n");

    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2)) << "the round did not continue";

    const QStringList ids = toolCallIdsOf(transport.streamRequest(1).payload());
    EXPECT_EQ(ids, (QStringList{"call_1"}))
        << "a delta whose id is present but empty must extend the open call, not open a new one";
    EXPECT_EQ(tool->calls, 1);

    transport.lastStream()->sendAll(finalTurn("done"));
    pump();
    EXPECT_EQ(completed.count(), 1);
}

namespace {

Conversation oneUserTurn(const QString &text)
{
    Conversation conversation;
    conversation.addUser(text);
    return conversation;
}

QJsonObject firstStreamPayload(const FakeHttpTransport &transport)
{
    return transport.streamRequest(0).payload();
}

} // namespace

TEST(ToolDefinitions, ClaudeAskCarriesTheRegisteredDefinition)
{
    FakeHttpTransport transport;
    ClaudeClient client("http://fake.local", "sk-test", "claude-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    client.ask(oneUserTurn(QStringLiteral("go")));
    ASSERT_EQ(transport.streamCount(), 1);

    const QJsonArray tools = firstStreamPayload(transport).value("tools").toArray();
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools[0].toObject().value("name").toString(), QStringLiteral("echo"));
    EXPECT_TRUE(tools[0].toObject().contains("input_schema"));
}

TEST(ToolDefinitions, OpenAIAskCarriesTheRegisteredDefinition)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    client.ask(oneUserTurn(QStringLiteral("go")));
    ASSERT_EQ(transport.streamCount(), 1);

    const QJsonArray tools = firstStreamPayload(transport).value("tools").toArray();
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools[0].toObject().value("type").toString(), QStringLiteral("function"));
    EXPECT_EQ(
        tools[0].toObject().value("function").toObject().value("name").toString(),
        QStringLiteral("echo"));
}

TEST(ToolDefinitions, GoogleAskCarriesTheRegisteredDefinition)
{
    FakeHttpTransport transport;
    GoogleAIClient client("http://fake.local", "key", "gemini-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    client.ask(oneUserTurn(QStringLiteral("go")));
    ASSERT_EQ(transport.streamCount(), 1);

    const QJsonArray tools = firstStreamPayload(transport).value("tools").toArray();
    ASSERT_EQ(tools.size(), 1);
    const QJsonArray declarations = tools[0].toObject().value("function_declarations").toArray();
    ASSERT_EQ(declarations.size(), 1);
    EXPECT_EQ(declarations[0].toObject().value("name").toString(), QStringLiteral("echo"));
}

TEST(ToolDefinitions, ResponsesAskCarriesTheRegisteredDefinition)
{
    FakeHttpTransport transport;
    OpenAIResponsesClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    client.ask(oneUserTurn(QStringLiteral("go")));
    ASSERT_EQ(transport.streamCount(), 1);

    const QJsonArray tools = firstStreamPayload(transport).value("tools").toArray();
    ASSERT_EQ(tools.size(), 1);
    const QJsonObject definition = tools[0].toObject();
    EXPECT_EQ(definition.value("type").toString(), QStringLiteral("function"));
    EXPECT_EQ(definition.value("name").toString(), QStringLiteral("echo"));
    EXPECT_FALSE(definition.contains("function"));
}

TEST(ToolDefinitions, OllamaAskCarriesTheRegisteredDefinition)
{
    FakeHttpTransport transport;
    OllamaClient client("http://fake.local", {}, "llama-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    client.ask(oneUserTurn(QStringLiteral("go")));
    ASSERT_EQ(transport.streamCount(), 1);

    const QJsonArray tools = firstStreamPayload(transport).value("tools").toArray();
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(
        tools[0].toObject().value("function").toObject().value("name").toString(),
        QStringLiteral("echo"));
}

TEST(ToolDefinitions, EmptyRegistryOmitsTheKeyEntirely)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools();

    client.ask(oneUserTurn(QStringLiteral("go")));
    ASSERT_EQ(transport.streamCount(), 1);

    EXPECT_FALSE(firstStreamPayload(transport).contains(QStringLiteral("tools")))
        << "an empty registry must not send \"tools\": [] -- providers differ on it";
}

TEST(ToolDefinitions, ExtraOverridesTheAttachedDefinitions)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    client.ask(oneUserTurn(QStringLiteral("go")), QJsonObject{{"tools", QJsonArray{}}});
    ASSERT_EQ(transport.streamCount(), 1);

    EXPECT_TRUE(firstStreamPayload(transport).value("tools").toArray().isEmpty())
        << "a host that names \"tools\" in extra still wins";
}

TEST(ToolDefinitions, DefinitionsSurviveIntoTheContinuationRequest)
{
    FakeHttpTransport transport;
    OpenAIClient client("http://fake.local/v1", "sk-test", "gpt-test", &transport);
    client.tools()->addTool(new CountingTool(&client));

    client.ask(oneUserTurn(QStringLiteral("go")));
    ASSERT_EQ(transport.streamCount(), 1);

    transport.lastStream()->sendAll(toolCallTurn("call_1"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(transport, 2));

    const QJsonArray tools = transport.streamRequest(1).payload().value("tools").toArray();
    EXPECT_EQ(tools.size(), 1) << "the continuation is built from the original payload";
}

TEST(ToolRounds, BufferedToolCallsProduceTheSameContinuationAsTheEquivalentStream)
{
    FakeHttpTransport streamTransport;
    OpenAIClient streamClient("http://fake.local/v1", "sk-test", "gpt-test", &streamTransport);
    streamClient.tools()->addTool(new CountingTool(&streamClient));

    streamClient.ask(oneUserTurn(QStringLiteral("go")));
    ASSERT_EQ(streamTransport.streamCount(), 1);
    streamTransport.lastStream()->sendAll(toolCallTurn("call_1"));
    ASSERT_TRUE(LLMQoreTest::waitForStreams(streamTransport, 2)) << "the stream did not continue";

    FakeHttpTransport bufferedTransport;
    OpenAIClient bufferedClient("http://fake.local/v1", "sk-test", "gpt-test", &bufferedTransport);
    bufferedClient.tools()->addTool(new CountingTool(&bufferedClient));

    bufferedClient.ask(oneUserTurn(QStringLiteral("go")), {}, RequestMode::Buffered);
    ASSERT_EQ(bufferedTransport.bufferedCount(), 1);
    bufferedTransport.respondToLast(
        200,
        R"({"choices":[{"message":{"role":"assistant","content":null,"tool_calls":[)"
        R"({"id":"call_1","type":"function","function":{"name":"echo","arguments":"{}"}}]},)"
        R"("finish_reason":"tool_calls"}]})");
    pump();
    ASSERT_EQ(bufferedTransport.bufferedCount(), 2) << "the buffered round did not continue";

    const QJsonObject fromStream = streamTransport.streamRequest(1).payload();
    const QJsonObject fromBuffered = bufferedTransport.bufferedRequest(1).payload();

    EXPECT_EQ(fromBuffered.value("messages"), fromStream.value("messages"))
        << "buffered and streamed turns must reach the wire as the same continuation";
    EXPECT_EQ(fromBuffered.value("tools"), fromStream.value("tools"));
}
