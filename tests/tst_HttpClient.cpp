// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <LLMCore/HttpClient.hpp>
#include <LLMCore/HttpResponse.hpp>
#include <LLMCore/HttpStream.hpp>
#include <LLMCore/HttpTransportError.hpp>

using namespace LLMCore;

namespace {

// Minimal HTTP/1.1 test server. Each pending connection is handed off to
// a per-test callback that decides the response (raw bytes, ordered chunks,
// or "never respond" for timeout tests). Parses just enough to detect the
// end of the request headers (a CRLFCRLF terminator) plus Content-Length
// body, then invokes the callback.
class HttpTestServer : public QObject
{
    Q_OBJECT
public:
    // Callback gets the raw request bytes and the socket. It's responsible
    // for driving the response (writing bytes, calling disconnectFromHost,
    // or leaving the socket idle for the timeout tests).
    using Handler = std::function<void(const QByteArray &request, QTcpSocket *socket)>;

    explicit HttpTestServer(QObject *parent = nullptr)
        : QObject(parent)
        , m_server(new QTcpServer(this))
    {
        connect(m_server, &QTcpServer::newConnection, this, &HttpTestServer::onNewConnection);
    }

    bool listen()
    {
        return m_server->listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return m_server->serverPort(); }

    QUrl url(const QString &path = QStringLiteral("/")) const
    {
        return QUrl(QString("http://127.0.0.1:%1%2").arg(m_server->serverPort()).arg(path));
    }

    void setHandler(Handler handler) { m_handler = std::move(handler); }

    int requestCount() const { return m_requestCount; }

private slots:
    void onNewConnection()
    {
        while (auto *socket = m_server->nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
                buffer->append(socket->readAll());
                // Look for end of headers.
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                // Parse Content-Length, if any.
                int contentLength = 0;
                const QByteArray headersBlob = buffer->left(headerEnd);
                for (const QByteArray &line : headersBlob.split('\n')) {
                    if (line.startsWith("Content-Length:") || line.startsWith("content-length:")) {
                        contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                        break;
                    }
                }
                const int totalNeeded = headerEnd + 4 + contentLength;
                if (buffer->size() < totalNeeded)
                    return;

                ++m_requestCount;
                const QByteArray request = buffer->left(totalNeeded);
                if (m_handler)
                    m_handler(request, socket);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

private:
    QTcpServer *m_server;
    Handler m_handler;
    int m_requestCount = 0;
};

// Pumps the event loop until `pred` returns true or timeoutMs elapses.
template<typename Pred>
bool waitFor(Pred pred, int timeoutMs = 5000)
{
    QEventLoop loop;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(20);
    QElapsedTimer elapsed;
    elapsed.start();
    while (!pred()) {
        if (elapsed.elapsed() >= timeoutMs)
            return false;
        loop.processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

// Blocks the event loop on a QFuture<T> until it resolves or timeout elapses.
// Returns the resolved value via out-param; on timeout leaves it default.
template<typename T>
bool waitForFuture(QFuture<T> future, T &out, int timeoutMs = 5000)
{
    QFutureWatcher<T> watcher;
    QEventLoop loop;
    bool finished = false;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, [&]() {
        finished = true;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();
    if (!finished)
        return false;
    try {
        out = future.result();
    } catch (...) {
        throw;
    }
    return true;
}

class HttpClientTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_server = std::make_unique<HttpTestServer>();
        ASSERT_TRUE(m_server->listen());
        m_client = std::make_unique<HttpClient>();
    }

    void TearDown() override
    {
        m_client.reset();
        m_server.reset();
    }

    std::unique_ptr<HttpTestServer> m_server;
    std::unique_ptr<HttpClient> m_client;
};

// Writes a canned HTTP/1.1 response with headers and body, then closes.
void writeResponse(
    QTcpSocket *socket,
    int status,
    const QString &reason,
    const QList<QPair<QByteArray, QByteArray>> &headers,
    const QByteArray &body)
{
    QByteArray out;
    out.append("HTTP/1.1 " + QByteArray::number(status) + " " + reason.toUtf8() + "\r\n");
    bool haveContentLength = false;
    for (const auto &h : headers) {
        out.append(h.first + ": " + h.second + "\r\n");
        if (h.first.compare("Content-Length", Qt::CaseInsensitive) == 0)
            haveContentLength = true;
    }
    if (!haveContentLength)
        out.append("Content-Length: " + QByteArray::number(body.size()) + "\r\n");
    out.append("\r\n");
    out.append(body);
    socket->write(out);
    socket->disconnectFromHost();
}

} // namespace

TEST_F(HttpClientTest, BufferedPostReturns200WithJsonBody)
{
    m_server->setHandler([](const QByteArray &, QTcpSocket *socket) {
        writeResponse(
            socket,
            200,
            "OK",
            {{"Content-Type", "application/json"}},
            R"({"ok":true})");
    });

    QNetworkRequest req(m_server->url("/echo"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    HttpResponse response;
    ASSERT_TRUE(waitForFuture(
        m_client->send(req, QByteArrayView("POST"), R"({"k":"v"})"), response));

    EXPECT_EQ(response.statusCode, 200);
    EXPECT_TRUE(response.isSuccess());
    EXPECT_EQ(response.body, R"({"ok":true})");
    EXPECT_EQ(response.contentType(), QLatin1String("application/json"));
}

TEST_F(HttpClientTest, Buffered404DoesNotThrow)
{
    m_server->setHandler([](const QByteArray &, QTcpSocket *socket) {
        writeResponse(
            socket,
            404,
            "Not Found",
            {{"Content-Type", "application/json"}},
            R"({"error":"nope"})");
    });

    QNetworkRequest req(m_server->url("/missing"));
    HttpResponse response;
    ASSERT_TRUE(waitForFuture(
        m_client->send(req, QByteArrayView("GET")), response));

    EXPECT_EQ(response.statusCode, 404);
    EXPECT_FALSE(response.isSuccess());
    EXPECT_EQ(response.body, R"({"error":"nope"})");
}

TEST_F(HttpClientTest, Buffered500DoesNotThrow)
{
    m_server->setHandler([](const QByteArray &, QTcpSocket *socket) {
        writeResponse(socket, 500, "Internal Error", {{"Content-Type", "text/plain"}}, "oops");
    });

    QNetworkRequest req(m_server->url("/fail"));
    HttpResponse response;
    ASSERT_TRUE(waitForFuture(
        m_client->send(req, QByteArrayView("GET")), response));

    EXPECT_EQ(response.statusCode, 500);
    EXPECT_FALSE(response.isSuccess());
    EXPECT_EQ(response.body, "oops");
}

TEST_F(HttpClientTest, BufferedHandles202EmptyBody)
{
    m_server->setHandler([](const QByteArray &, QTcpSocket *socket) {
        writeResponse(socket, 202, "Accepted", {}, QByteArray());
    });

    QNetworkRequest req(m_server->url());
    HttpResponse response;
    ASSERT_TRUE(waitForFuture(
        m_client->send(req, QByteArrayView("POST"), "{}"), response));

    EXPECT_EQ(response.statusCode, 202);
    EXPECT_TRUE(response.isSuccess());
    EXPECT_TRUE(response.body.isEmpty());
}

TEST_F(HttpClientTest, RawHeaderLookupIsCaseInsensitive)
{
    m_server->setHandler([](const QByteArray &request, QTcpSocket *socket) {
        // Echo a received custom header back in response headers so we can
        // verify round-tripping.
        QByteArray received;
        for (const QByteArray &line : request.split('\n')) {
            if (line.startsWith("X-Custom:") || line.startsWith("x-custom:")) {
                received = line.mid(line.indexOf(':') + 1).trimmed();
                break;
            }
        }
        writeResponse(
            socket,
            200,
            "OK",
            {{"X-Echo", received}, {"Mcp-Session-Id", "sid-abc"}},
            "body");
    });

    QNetworkRequest req(m_server->url());
    req.setRawHeader("X-Custom", "hello");

    HttpResponse response;
    ASSERT_TRUE(waitForFuture(
        m_client->send(req, QByteArrayView("GET")), response));

    EXPECT_EQ(response.statusCode, 200);
    EXPECT_EQ(response.rawHeader(QByteArrayView("x-echo")), "hello");
    EXPECT_EQ(response.rawHeader(QByteArrayView("X-Echo")), "hello");
    EXPECT_EQ(response.rawHeader(QByteArrayView("mcp-session-id")), "sid-abc");
    EXPECT_TRUE(response.hasRawHeader(QByteArrayView("MCP-SESSION-ID")));
    EXPECT_FALSE(response.hasRawHeader(QByteArrayView("not-there")));
}

TEST_F(HttpClientTest, TransportErrorOnConnectionRefused)
{
    // Use a port that almost certainly is not listening. 1 is reserved by
    // convention and connection-refused is reliable on localhost.
    QNetworkRequest req(QUrl("http://127.0.0.1:1/"));
    auto future = m_client->send(req, QByteArrayView("GET"));

    QFutureWatcher<HttpResponse> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<HttpResponse>::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();

    ASSERT_TRUE(future.isFinished());
    bool caughtTransportError = false;
    try {
        future.result();
    } catch (const HttpTransportError &e) {
        caughtTransportError = true;
        EXPECT_FALSE(e.message().isEmpty());
    } catch (const std::exception &) {
        FAIL() << "Expected HttpTransportError, got std::exception base";
    }
    EXPECT_TRUE(caughtTransportError);
}

TEST_F(HttpClientTest, TransportErrorOnTimeout)
{
    // Handler never responds; short timeout forces TimeoutError.
    m_server->setHandler([](const QByteArray &, QTcpSocket *) {});
    m_client->setTransferTimeout(300);

    QNetworkRequest req(m_server->url());
    auto future = m_client->send(req, QByteArrayView("POST"), "{}");

    QFutureWatcher<HttpResponse> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<HttpResponse>::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    watcher.setFuture(future);
    loop.exec();

    ASSERT_TRUE(future.isFinished());
    bool caughtTransportError = false;
    try {
        future.result();
    } catch (const HttpTransportError &e) {
        caughtTransportError = true;
        // Qt maps transfer-timeout to OperationCanceledError in 6.x.
        EXPECT_TRUE(
            e.networkError() == QNetworkReply::OperationCanceledError
            || e.networkError() == QNetworkReply::TimeoutError);
    }
    EXPECT_TRUE(caughtTransportError);
}

TEST_F(HttpClientTest, StreamingDeliversChunks)
{
    // Server responds with a small body split manually into two writes to
    // encourage multiple readyRead signals on the client side. We use
    // Content-Length so the client knows when the response is complete.
    m_server->setHandler([](const QByteArray &, QTcpSocket *socket) {
        QByteArray headers = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 10\r\n\r\n";
        socket->write(headers);
        socket->write("hello");
        socket->flush();
        QTimer::singleShot(20, socket, [socket]() {
            socket->write("world");
            socket->disconnectFromHost();
        });
    });

    QNetworkRequest req(m_server->url());
    HttpStream *stream = m_client->openStream(req, QByteArrayView("GET"));
    ASSERT_NE(stream, nullptr);

    QByteArray received;
    bool finished = false;
    bool headersFired = false;
    int chunkCount = 0;
    QObject::connect(stream, &HttpStream::headersReceived, stream, [&]() {
        headersFired = true;
    });
    QObject::connect(
        stream, &HttpStream::chunkReceived, stream, [&](const QByteArray &chunk) {
            received += chunk;
            ++chunkCount;
        });
    QObject::connect(stream, &HttpStream::finished, stream, [&]() { finished = true; });

    ASSERT_TRUE(waitFor([&]() { return finished; }));
    EXPECT_TRUE(headersFired);
    EXPECT_EQ(stream->statusCode(), 200);
    EXPECT_EQ(received, "helloworld");
    EXPECT_GE(chunkCount, 1);

    stream->deleteLater();
}

TEST_F(HttpClientTest, StreamingSurfaces500WithBody)
{
    m_server->setHandler([](const QByteArray &, QTcpSocket *socket) {
        writeResponse(socket, 503, "Service Unavailable", {{"Content-Type", "text/plain"}}, "down");
    });

    QNetworkRequest req(m_server->url());
    HttpStream *stream = m_client->openStream(req, QByteArrayView("POST"), "{}");
    ASSERT_NE(stream, nullptr);

    QByteArray received;
    bool finished = false;
    QObject::connect(
        stream, &HttpStream::chunkReceived, stream, [&](const QByteArray &chunk) {
            received += chunk;
        });
    QObject::connect(stream, &HttpStream::finished, stream, [&]() { finished = true; });

    ASSERT_TRUE(waitFor([&]() { return finished; }));
    EXPECT_EQ(stream->statusCode(), 503);
    EXPECT_EQ(received, "down");

    stream->deleteLater();
}

TEST_F(HttpClientTest, StreamingAbortSuppressesFurtherSignals)
{
    // Handler writes part of the body but never finishes — we abort mid-stream.
    m_server->setHandler([](const QByteArray &, QTcpSocket *socket) {
        QByteArray headers
            = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 100\r\n\r\n";
        socket->write(headers);
        socket->write("xxxxx");
        socket->flush();
        // Never writes the rest; caller must abort.
    });

    QNetworkRequest req(m_server->url());
    HttpStream *stream = m_client->openStream(req, QByteArrayView("GET"));
    ASSERT_NE(stream, nullptr);

    bool finishedFired = false;
    bool errorFired = false;
    QObject::connect(stream, &HttpStream::finished, stream, [&]() { finishedFired = true; });
    QObject::connect(
        stream, &HttpStream::errorOccurred, stream, [&](const HttpTransportError &) {
            errorFired = true;
        });

    bool chunkFired = false;
    QObject::connect(stream, &HttpStream::chunkReceived, stream, [&](const QByteArray &) {
        chunkFired = true;
    });

    // Wait for at least one chunk to confirm headers flowed through, then abort.
    ASSERT_TRUE(waitFor([&]() { return chunkFired; }));
    stream->abort();

    // Spin the event loop briefly to give Qt a chance to deliver any
    // pending signals — we expect none.
    QEventLoop pump;
    QTimer::singleShot(150, &pump, &QEventLoop::quit);
    pump.exec();

    EXPECT_FALSE(finishedFired);
    EXPECT_FALSE(errorFired);
    EXPECT_TRUE(stream->isFinished());

    stream->deleteLater();
}

#include "tst_HttpClient.moc"
