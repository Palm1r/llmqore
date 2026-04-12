// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpHttpServerTransport.hpp>

#include <LLMCore/HttpRequestParser.hpp>
#include <LLMCore/Log.hpp>
#include <LLMCore/SSEParser.hpp>

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

namespace LLMCore::Mcp {

namespace {

struct PendingRequestEntry
{
    QPointer<QTcpSocket> socket;
    QTimer *timeoutTimer = nullptr;
};

QString jsonRpcIdToString(const QJsonValue &idValue)
{
    if (idValue.isString())
        return idValue.toString();
    if (idValue.isDouble())
        return QString::number(static_cast<qint64>(idValue.toDouble()));
    return {};
}

QByteArray sseEventForMessage(const QJsonObject &msg)
{
    return SSEParser::format(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

} // namespace

struct McpHttpServerTransport::Impl
{
    McpHttpServerTransport *q = nullptr;
    HttpServerConfig config;
    QTcpServer *server = nullptr;
    bool open = false;
    QString sessionId;

    QHash<QTcpSocket *, HttpRequestParser> connections;

    QHash<QString, PendingRequestEntry> pendingByRequestId;

    QList<QJsonObject> queuedServerMessages;

    using HeaderList = QList<QPair<QByteArray, QByteArray>>;
    static QPair<QByteArray, QByteArray> h(const char *k, const char *v)
    {
        return {QByteArray(k), QByteArray(v)};
    }

    void onNewConnection()
    {
        while (QTcpSocket *client = server->nextPendingConnection()) {
            HttpRequestParser parser;
            parser.setMaxHeaderBytes(config.maxHeaderBytes);
            parser.setMaxBodyBytes(config.maxBodyBytes);
            connections.insert(client, std::move(parser));
            QObject::connect(client, &QTcpSocket::readyRead, q, [this, client]() {
                onReadyRead(client);
            });
            QObject::connect(client, &QTcpSocket::disconnected, q, [this, client]() {
                onDisconnected(client);
            });
        }
    }

    void onDisconnected(QTcpSocket *client)
    {
        connections.remove(client);
        for (auto it = pendingByRequestId.begin(); it != pendingByRequestId.end();) {
            if (it.value().socket.isNull() || it.value().socket.data() == client) {
                if (it.value().timeoutTimer) {
                    it.value().timeoutTimer->stop();
                    it.value().timeoutTimer->deleteLater();
                }
                it = pendingByRequestId.erase(it);
            } else {
                ++it;
            }
        }
        client->deleteLater();
    }

    void enforceQueueBudget()
    {
        const int cap = config.maxQueuedServerMessages;
        if (cap <= 0)
            return;
        while (queuedServerMessages.size() > cap) {
            queuedServerMessages.removeFirst();
            qCWarning(llmMcpLog).noquote()
                << "Queued server-initiated message dropped: queue is full";
        }
    }

    void respondWithStatus(QTcpSocket *client, int status, const QByteArray &reason)
    {
        writeStatus(
            client, status, reason, HeaderList{h("Content-Length", "0"), h("Connection", "close")});
        client->disconnectFromHost();
    }

    void onReadyRead(QTcpSocket *client)
    {
        auto it = connections.find(client);
        if (it == connections.end())
            return;

        HttpRequestParser &parser = *it;
        parser.feed(client->readAll());

        while (true) {
            switch (parser.parseNext()) {
            case HttpRequestParser::Status::NeedMoreData:
                return;

            case HttpRequestParser::Status::Complete: {
                HttpRequest req = parser.takeRequest();
                processRequest(client, req);
                if (!connections.contains(client)
                    || client->state() != QAbstractSocket::ConnectedState)
                    return;
                continue;
            }

            case HttpRequestParser::Status::HeaderOverflow:
                qCWarning(llmMcpLog).noquote()
                    << QString("Rejecting request: header block exceeds %1 bytes")
                           .arg(config.maxHeaderBytes);
                respondWithStatus(client, 431, "Request Header Fields Too Large");
                return;

            case HttpRequestParser::Status::BodyTooLarge:
                qCWarning(llmMcpLog).noquote()
                    << QString("Rejecting request: body exceeds %1 bytes")
                           .arg(config.maxBodyBytes);
                respondWithStatus(client, 413, "Payload Too Large");
                return;

            case HttpRequestParser::Status::InvalidContentLength:
                qCWarning(llmMcpLog).noquote()
                    << "Rejecting request: invalid Content-Length";
                respondWithStatus(client, 400, "Bad Request");
                return;

            case HttpRequestParser::Status::InvalidRequestLine:
                qCWarning(llmMcpLog).noquote()
                    << "Rejecting request: malformed request line";
                respondWithStatus(client, 400, "Bad Request");
                return;
            }
        }
    }

    void processRequest(QTcpSocket *client, const HttpRequest &req)
    {
        if (req.method != "POST") {
            writeStatus(
                client,
                405,
                "Method Not Allowed",
                HeaderList{h("Allow", "POST"), h("Content-Length", "0"), h("Connection", "close")});
            client->disconnectFromHost();
            return;
        }

        const QByteArray targetPath = req.target.split('?').first();
        if (targetPath != config.path.toUtf8()) {
            respondWithStatus(client, 404, "Not Found");
            return;
        }

        if (!config.allowedOrigins.isEmpty()) {
            const QString origin = QString::fromUtf8(req.header(QByteArrayView("origin")));
            if (!config.allowedOrigins.contains(origin)) {
                qCWarning(llmMcpLog).noquote()
                    << QString("Rejecting POST from disallowed Origin: %1").arg(origin);
                respondWithStatus(client, 403, "Forbidden");
                return;
            }
        }

        const QByteArray incomingSession = req.header(QByteArrayView("mcp-session-id"));
        if (!incomingSession.isEmpty() && QString::fromUtf8(incomingSession) != sessionId) {
            respondWithStatus(client, 400, "Bad Request");
            return;
        }

        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(req.body, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            respondWithStatus(client, 400, "Bad Request");
            return;
        }

        const QJsonObject msg = doc.object();
        if (msg.value("jsonrpc").toString() != QLatin1String("2.0")) {
            respondWithStatus(client, 400, "Bad Request");
            return;
        }
        const bool hasId = msg.contains("id") && !msg.value("id").isNull();
        const bool hasMethod = msg.contains("method");

        if (hasMethod && hasId) {
            const QString idStr = jsonRpcIdToString(msg.value("id"));
            if (!idStr.isEmpty()) {
                PendingRequestEntry entry;
                entry.socket = QPointer<QTcpSocket>(client);
                entry.timeoutTimer = new QTimer(q);
                entry.timeoutTimer->setSingleShot(true);
                entry.timeoutTimer->setInterval(config.pendingResponseTimeoutMs);
                QObject::connect(entry.timeoutTimer, &QTimer::timeout, q, [this, idStr]() {
                    auto it = pendingByRequestId.find(idStr);
                    if (it == pendingByRequestId.end())
                        return;
                    QTcpSocket *sock = it.value().socket.data();
                    if (it.value().timeoutTimer)
                        it.value().timeoutTimer->deleteLater();
                    pendingByRequestId.erase(it);
                    qCWarning(llmMcpLog).noquote()
                        << QString("Pending JSON-RPC request id=%1 timed out, closing socket")
                               .arg(idStr);
                    if (sock && sock->state() == QAbstractSocket::ConnectedState) {
                        writeStatus(
                            sock,
                            504,
                            "Gateway Timeout",
                            HeaderList{h("Content-Length", "0"), h("Connection", "close")});
                        sock->disconnectFromHost();
                    }
                });
                entry.timeoutTimer->start();
                pendingByRequestId.insert(idStr, entry);
            }
            emit q->messageReceived(msg);
            return;
        }

        emit q->messageReceived(msg);
        writeAckOrQueuedMessages(client);
    }

    void writeAckOrQueuedMessages(QTcpSocket *client)
    {
        if (queuedServerMessages.isEmpty()) {
            writeStatus(
                client,
                202,
                "Accepted",
                standardResponseHeaders(HeaderList{h("Content-Length", "0")}));
            client->disconnectFromHost();
            return;
        }

        QByteArray body;
        for (const QJsonObject &m : queuedServerMessages)
            body.append(sseEventForMessage(m));
        queuedServerMessages.clear();

        writeSseResponse(client, body);
        client->disconnectFromHost();
    }

    void writeStatus(
        QTcpSocket *client,
        int status,
        const QByteArray &reason,
        const HeaderList &extraHeaders)
    {
        QByteArray out;
        out.append("HTTP/1.1 ");
        out.append(QByteArray::number(status));
        out.append(' ');
        out.append(reason);
        out.append("\r\n");
        for (const auto &h : extraHeaders) {
            out.append(h.first);
            out.append(": ");
            out.append(h.second);
            out.append("\r\n");
        }
        out.append("\r\n");
        client->write(out);
        client->flush();
    }

    HeaderList standardResponseHeaders(const HeaderList &extra = {}) const
    {
        HeaderList headers;
        headers.append({QByteArray("Mcp-Session-Id"), sessionId.toUtf8()});
        headers.append(h("Access-Control-Allow-Origin", "*"));
        headers.append(h("Cache-Control", "no-store"));
        headers.append(extra);
        return headers;
    }

    void writeSseResponse(QTcpSocket *client, const QByteArray &body)
    {
        HeaderList headers = standardResponseHeaders(HeaderList{
            h("Content-Type", "text/event-stream"),
            {QByteArray("Content-Length"), QByteArray::number(body.size())},
        });
        writeStatus(client, 200, "OK", headers);
        client->write(body);
        client->flush();
    }

    void writeJsonResponse(QTcpSocket *client, const QJsonObject &message)
    {
        const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
        HeaderList headers = standardResponseHeaders(HeaderList{
            h("Content-Type", "application/json"),
            {QByteArray("Content-Length"), QByteArray::number(body.size())},
        });
        writeStatus(client, 200, "OK", headers);
        client->write(body);
        client->flush();
    }
};

McpHttpServerTransport::McpHttpServerTransport(HttpServerConfig config, QObject *parent)
    : McpTransport(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->q = this;
    m_impl->config = std::move(config);
    m_impl->server = new QTcpServer(this);
    m_impl->sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QObject::connect(m_impl->server, &QTcpServer::newConnection, this, [this]() {
        m_impl->onNewConnection();
    });
}

McpHttpServerTransport::~McpHttpServerTransport()
{
    stop();
}

void McpHttpServerTransport::start()
{
    if (m_impl->open)
        return;
    setState(State::Connecting);
    if (!m_impl->server->listen(m_impl->config.address, m_impl->config.port)) {
        emit errorOccurred(
            QString("Failed to listen on %1:%2: %3")
                .arg(m_impl->config.address.toString())
                .arg(m_impl->config.port)
                .arg(m_impl->server->errorString()));
        setState(State::Failed);
        return;
    }
    m_impl->open = true;
    setState(State::Connected);
    qCDebug(llmMcpLog).noquote()
        << QString("MCP HTTP server listening on %1:%2%3")
               .arg(m_impl->server->serverAddress().toString())
               .arg(m_impl->server->serverPort())
               .arg(m_impl->config.path);
}

void McpHttpServerTransport::stop()
{
    if (!m_impl->open)
        return;
    m_impl->open = false;

    const auto sockets = m_impl->connections.keys();
    for (QTcpSocket *client : sockets) {
        client->disconnect(this);
        client->abort();
        client->deleteLater();
    }
    m_impl->connections.clear();
    for (auto &entry : m_impl->pendingByRequestId) {
        if (entry.timeoutTimer) {
            entry.timeoutTimer->stop();
            entry.timeoutTimer->deleteLater();
        }
    }
    m_impl->pendingByRequestId.clear();
    m_impl->queuedServerMessages.clear();

    if (m_impl->server->isListening())
        m_impl->server->close();

    setState(State::Disconnected);
    emit closed();
}

bool McpHttpServerTransport::isOpen() const
{
    return m_impl->open;
}

void McpHttpServerTransport::send(const QJsonObject &message)
{
    if (!m_impl->open) {
        emit errorOccurred(QStringLiteral("Transport not open"));
        return;
    }

    if (message.contains("id") && !message.value("id").isNull()
        && (message.contains("result") || message.contains("error"))) {
        const QString idStr = jsonRpcIdToString(message.value("id"));
        auto it = m_impl->pendingByRequestId.find(idStr);
        if (it != m_impl->pendingByRequestId.end()) {
            QTcpSocket *client = it.value().socket.data();
            if (it.value().timeoutTimer) {
                it.value().timeoutTimer->stop();
                it.value().timeoutTimer->deleteLater();
            }
            m_impl->pendingByRequestId.erase(it);
            if (!client || client->state() != QAbstractSocket::ConnectedState) {
                qCWarning(llmMcpLog).noquote()
                    << QString("Dropping response for id=%1: socket gone").arg(idStr);
                return;
            }

            if (m_impl->queuedServerMessages.isEmpty()) {
                m_impl->writeJsonResponse(client, message);
            } else {
                QByteArray body;
                for (const QJsonObject &m : m_impl->queuedServerMessages)
                    body.append(sseEventForMessage(m));
                body.append(sseEventForMessage(message));
                m_impl->queuedServerMessages.clear();
                m_impl->writeSseResponse(client, body);
            }
            client->disconnectFromHost();
            return;
        }
        qCWarning(llmMcpLog).noquote()
            << QString("send() response for id=%1 has no pending socket — dropping")
                   .arg(idStr);
        return;
    }

    m_impl->queuedServerMessages.append(message);
    m_impl->enforceQueueBudget();
    qCDebug(llmMcpLog).noquote()
        << QString("Queued server-initiated message; %1 pending for next flush")
               .arg(m_impl->queuedServerMessages.size());
}

quint16 McpHttpServerTransport::serverPort() const
{
    return m_impl->server ? m_impl->server->serverPort() : quint16(0);
}

const HttpServerConfig &McpHttpServerTransport::config() const
{
    return m_impl->config;
}

QString McpHttpServerTransport::sessionId() const
{
    return m_impl->sessionId;
}

} // namespace LLMCore::Mcp
