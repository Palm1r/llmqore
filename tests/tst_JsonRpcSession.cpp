// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QSignalSpy>

#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/JsonRpcSession.hpp>
#include <LLMQore/RpcExceptions.hpp>
#include <LLMQore/RpcPipeTransport.hpp>

#include "LoopbackHarness.hpp"
#include "TestHelpers.hpp"

using namespace LLMQore;
using LLMQoreTest::SessionPair;

namespace {

class JsonRpcSessionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "tst_JsonRpcSession";
            static char *argv[] = {arg0};
            m_app = new QCoreApplication(argc, argv);
        }
    }
    void TearDown() override
    {
        delete m_app;
        m_app = nullptr;
    }
    QCoreApplication *m_app = nullptr;
};

QString failureOf(QFuture<QJsonValue> future, std::chrono::milliseconds timeout = kDefaultWaitTimeout)
{
    if (!future.isFinished()) {
        QEventLoop loop;
        QFutureWatcher<QJsonValue> watcher;
        QObject::connect(
            &watcher, &QFutureWatcher<QJsonValue>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
        loop.exec();
    }

    if (!future.isFinished())
        return QStringLiteral("<never finished>");

    try {
        future.result();
    } catch (const Rpc::JsonRpcException &e) {
        return e.message();
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }
    return {};
}

class HeldAnswer
{
public:
    LLMQore::Rpc::JsonRpcSession::RequestHandler handler()
    {
        return [this](const QJsonObject &) -> QFuture<QJsonValue> {
            auto promise = std::make_shared<QPromise<QJsonValue>>();
            promise->start();
            m_promises.append(promise);
            return promise->future();
        };
    }

    void releaseAll()
    {
        for (auto &promise : m_promises) {
            promise->addResult(QJsonValue(QJsonObject{}));
            promise->finish();
        }
        m_promises.clear();
    }

private:
    QList<std::shared_ptr<QPromise<QJsonValue>>> m_promises;
};

} // namespace

TEST_F(JsonRpcSessionTest, ARequestThatIsNeverAnsweredFailsWithATimeout)
{
    SessionPair pair;
    HeldAnswer held;
    pair.remote->setRequestHandler(QStringLiteral("slow"), held.handler());

    const QString error = failureOf(
        pair.local->sendRequest(QStringLiteral("slow"), {}, std::chrono::milliseconds(150)));

    EXPECT_TRUE(error.contains(QStringLiteral("timed out"), Qt::CaseInsensitive))
        << "a request left unanswered must fail on its own timer, not hang: "
        << qPrintable(error);

    held.releaseAll();
}

TEST_F(JsonRpcSessionTest, AbortPendingFailsEveryOutstandingRequest)
{
    SessionPair pair;
    HeldAnswer held;
    pair.remote->setRequestHandler(QStringLiteral("slow"), held.handler());

    QFuture<QJsonValue> first
        = pair.local->sendRequest(QStringLiteral("slow"), {}, std::chrono::seconds(30));
    QFuture<QJsonValue> second
        = pair.local->sendRequest(QStringLiteral("slow"), {}, std::chrono::seconds(30));

    pair.local->abortPending(QStringLiteral("going away"));

    EXPECT_EQ(failureOf(first), QStringLiteral("going away"));
    EXPECT_EQ(failureOf(second), QStringLiteral("going away"))
        << "abortPending must reach every request, not just the first";

    held.releaseAll();
}

TEST_F(JsonRpcSessionTest, ClosingTheTransportFailsPendingRequests)
{
    SessionPair pair;
    HeldAnswer held;
    pair.remote->setRequestHandler(QStringLiteral("slow"), held.handler());

    QFuture<QJsonValue> pending
        = pair.local->sendRequest(QStringLiteral("slow"), {}, std::chrono::seconds(30));

    pair.localTransport->stop();

    EXPECT_FALSE(failureOf(pending).isEmpty())
        << "a request whose transport died must not stay pending forever";

    held.releaseAll();
}

TEST_F(JsonRpcSessionTest, AFailedSendFailsItsRequestAtOnce)
{
    SessionPair pair;
    HeldAnswer held;
    pair.remote->setRequestHandler(QStringLiteral("slow"), held.handler());

    const auto request = pair.local->sendCancellableRequest(
        QStringLiteral("slow"), {}, std::chrono::seconds(30));
    const QJsonObject sent{{"jsonrpc", "2.0"}, {"id", request.requestId}, {"method", "slow"}};

    emit pair.localTransport->sendFailed(sent, QStringLiteral("HTTP 503: upstream down"));

    const QString failure = failureOf(request.future, std::chrono::milliseconds(500));
    EXPECT_EQ(failure, QStringLiteral("HTTP 503: upstream down"))
        << "the request must fail with the transport's reason, not wait for its timer, got "
        << qPrintable(failure);

    bool isTransportError = false;
    if (request.future.isFinished()) {
        try {
            request.future.result();
        } catch (const Rpc::TransportError &) {
            isTransportError = true;
        } catch (...) {
        }
    }
    EXPECT_TRUE(isTransportError) << "a failed send is the transport's failure, not the peer's";

    held.releaseAll();
}

TEST_F(JsonRpcSessionTest, CancellingAnOutgoingRequestTellsThePeerAndDropsTheAnswer)
{
    SessionPair pair;

    QStringList cancelledIds;
    pair.remote->setNotificationHandler(
        QLatin1String(Rpc::Method::Cancelled), [&cancelledIds](const QJsonObject &params) {
            cancelledIds << params.value(QStringLiteral("requestId")).toString();
        });
    HeldAnswer held;
    pair.remote->setRequestHandler(QStringLiteral("slow"), held.handler());

    Rpc::JsonRpcSession::CancellableRequest call
        = pair.local->sendCancellableRequest(QStringLiteral("slow"));
    ASSERT_FALSE(call.requestId.isEmpty());

    pair.local->cancelRequest(call.requestId, QStringLiteral("user pressed stop"));

    EXPECT_FALSE(failureOf(call.future).isEmpty()) << "the caller's future must not stay open";
    pumpEventLoop(std::chrono::milliseconds(50));
    EXPECT_EQ(cancelledIds, QStringList{call.requestId})
        << "the peer must be told to stop working";

    held.releaseAll();
}

TEST_F(JsonRpcSessionTest, AnIncomingRequestIsReportedAsCancelledToItsHandler)
{
    SessionPair pair;

    auto workPromise = std::make_shared<QPromise<QJsonValue>>();
    pair.remote->setRequestHandler(
        QStringLiteral("slow"), [&](const QJsonObject &) -> QFuture<QJsonValue> {
            workPromise->start();
            return workPromise->future();
        });

    Rpc::JsonRpcSession::CancellableRequest call
        = pair.local->sendCancellableRequest(QStringLiteral("slow"));
    pumpEventLoop(std::chrono::milliseconds(50));

    pair.local->cancelRequest(call.requestId);
    pumpEventLoop(std::chrono::milliseconds(50));

    EXPECT_TRUE(pair.remote->isRequestCancelled(call.requestId))
        << "a handler must be able to notice that its caller walked away";

    workPromise->addResult(QJsonValue(QJsonObject{}));
    workPromise->finish();
    pumpEventLoop(std::chrono::milliseconds(50));

    EXPECT_FALSE(failureOf(call.future).isEmpty());
}

TEST_F(JsonRpcSessionTest, AnUnparseableMessageIsAnsweredWithInvalidRequest)
{
    SessionPair pair;

    QJsonObject reply;
    QObject::connect(
        pair.remoteTransport, &Rpc::Transport::messageReceived, [&reply](const QJsonObject &m) {
            if (m.contains(QStringLiteral("error")))
                reply = m;
        });

    pair.remoteTransport->send(QJsonObject{{"jsonrpc", "2.0"}, {"id", "x1"}});
    pumpEventLoop(std::chrono::milliseconds(50));

    ASSERT_FALSE(reply.isEmpty()) << "an unclassifiable message must be answered, not ignored";
    EXPECT_EQ(reply.value("error").toObject().value("code").toInt(),
              Rpc::ErrorCode::InvalidRequest);
    EXPECT_EQ(reply.value("id").toString(), QStringLiteral("x1"));
}

TEST_F(JsonRpcSessionTest, AMessageFromAnotherJsonRpcVersionIsDropped)
{
    SessionPair pair;
    QSignalSpy errors(pair.local, &Rpc::JsonRpcSession::protocolError);

    bool handlerRan = false;
    pair.local->setNotificationHandler(
        QStringLiteral("hello"), [&handlerRan](const QJsonObject &) { handlerRan = true; });

    pair.remoteTransport->send(QJsonObject{{"jsonrpc", "1.0"}, {"method", "hello"}});
    pumpEventLoop(std::chrono::milliseconds(50));

    EXPECT_FALSE(handlerRan) << "a message from another protocol version must not be dispatched";
    EXPECT_GE(errors.size(), 1) << "and the host should be able to see that it happened";
}

TEST_F(JsonRpcSessionTest, AResponseToAnUnknownIdIsIgnoredWithoutCrashing)
{
    SessionPair pair;

    bool handlerRan = false;
    pair.local->setNotificationHandler(
        QStringLiteral("still-alive"), [&handlerRan](const QJsonObject &) { handlerRan = true; });

    pair.remoteTransport->send(
        QJsonObject{{"jsonrpc", "2.0"}, {"id", "no-such-request"}, {"result", QJsonObject{}}});
    pair.remoteTransport->send(QJsonObject{{"jsonrpc", "2.0"}, {"method", "still-alive"}});
    pumpEventLoop(std::chrono::milliseconds(50));

    EXPECT_TRUE(handlerRan) << "a response nobody asked for must not take the session down";
}

TEST_F(JsonRpcSessionTest, AnUnknownMethodIsAnsweredWithMethodNotFound)
{
    SessionPair pair;

    const QString error = failureOf(pair.local->sendRequest(QStringLiteral("nope")));
    EXPECT_TRUE(error.contains(QString::number(Rpc::ErrorCode::MethodNotFound)))
        << qPrintable(error);
}

TEST_F(JsonRpcSessionTest, AThrowingHandlerBecomesAnErrorResponse)
{
    SessionPair pair;
    pair.remote->setRequestHandler(
        QStringLiteral("boom"), [](const QJsonObject &) -> QFuture<QJsonValue> {
            throw Rpc::RemoteError(Rpc::ErrorCode::InvalidParams, QStringLiteral("bad params"));
        });

    const QString error = failureOf(pair.local->sendRequest(QStringLiteral("boom")));
    EXPECT_TRUE(error.contains(QStringLiteral("bad params"))) << qPrintable(error);
    EXPECT_TRUE(error.contains(QString::number(Rpc::ErrorCode::InvalidParams)))
        << "the remote code must survive the trip: " << qPrintable(error);
}

TEST_F(JsonRpcSessionTest, AFailingFutureBecomesAnErrorResponse)
{
    SessionPair pair;
    pair.remote->setRequestHandler(
        QStringLiteral("later"), [](const QJsonObject &) -> QFuture<QJsonValue> {
            return LLMQore::failedFuture<QJsonValue>(
                Rpc::RemoteError(Rpc::ErrorCode::InternalError, QStringLiteral("it broke")));
        });

    const QString error = failureOf(pair.local->sendRequest(QStringLiteral("later")));
    EXPECT_TRUE(error.contains(QStringLiteral("it broke"))) << qPrintable(error);
}

TEST_F(JsonRpcSessionTest, ProgressReachesTheHandlerRegisteredForTheRequest)
{
    SessionPair pair;

    auto work = std::make_shared<QPromise<QJsonValue>>();
    pair.remote->setRequestHandler(
        QStringLiteral("long"), [&](const QJsonObject &) -> QFuture<QJsonValue> {
            work->start();
            pair.remote->sendProgress(pair.remote->currentProgressToken(), 1, 2, "half");
            return work->future();
        });

    QList<double> seen;
    Rpc::JsonRpcSession::CancellableRequest call
        = pair.local->sendCancellableRequest(QStringLiteral("long"));
    pair.local->setProgressHandler(
        call.requestId, [&seen](double progress, double, const QString &) { seen << progress; });

    pumpEventLoop(std::chrono::milliseconds(80));
    work->addResult(QJsonValue(QJsonObject{}));
    work->finish();
    pumpEventLoop(std::chrono::milliseconds(50));

    EXPECT_EQ(seen, QList<double>{1.0});
}
