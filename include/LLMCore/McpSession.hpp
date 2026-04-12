// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include <QAtomicInteger>
#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QSet>
#include <QString>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/McpTransport.hpp>

class QTimer;

namespace LLMCore::Mcp {

class LLMCORE_EXPORT McpSession : public QObject
{
    Q_OBJECT
public:
    explicit McpSession(McpTransport *transport, QObject *parent = nullptr);
    ~McpSession() override;

    QFuture<QJsonValue> sendRequest(
        const QString &method,
        const QJsonObject &params = {},
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    struct CancellableRequest
    {
        QFuture<QJsonValue> future;
        QString requestId; // same value used as the progress token
    };
    CancellableRequest sendCancellableRequest(
        const QString &method,
        const QJsonObject &params = {},
        std::chrono::milliseconds timeout = std::chrono::seconds(30));

    void cancelRequest(const QString &id, const QString &reason = {});

    void sendNotification(const QString &method, const QJsonObject &params = {});

    using RequestHandler = std::function<QFuture<QJsonValue>(const QJsonObject &params)>;
    using NotifyHandler = std::function<void(const QJsonObject &params)>;

    void setRequestHandler(const QString &method, RequestHandler handler);
    void setNotificationHandler(const QString &method, NotifyHandler handler);

    using ProgressHandler
        = std::function<void(double progress, double total, const QString &message)>;
    void setProgressHandler(const QString &progressToken, ProgressHandler handler);
    void clearProgressHandler(const QString &progressToken);

    void sendProgress(
        const QString &progressToken,
        double progress,
        double total = 0.0,
        const QString &message = {});

    QString currentProgressToken() const noexcept { return m_currentProgressToken; }

    bool isRequestCancelled(const QString &requestId) const;

    McpTransport *transport() const noexcept { return m_transport; }

    void abortPending(const QString &reason);

signals:
    void notificationReceived(const QString &method, const QJsonObject &params);
    void protocolError(const QString &reason);
    void incomingRequest(const QString &method);
    void progressReceived(
        const QString &progressToken, double progress, double total, const QString &message);

private slots:
    void onMessageReceived(const QJsonObject &message);
    void onTransportClosed();

private:
    QString allocateId();

    CancellableRequest sendRequestImpl(
        const QString &method,
        const QJsonObject &params,
        std::chrono::milliseconds timeout,
        bool trackProgressToken);

    void dispatchRequest(const QJsonObject &message);
    void dispatchResponse(const QJsonObject &message);
    void dispatchNotification(const QJsonObject &message);

    void sendResponse(const QJsonValue &id, const QJsonValue &result);
    void sendError(const QJsonValue &id, int code, const QString &message, const QJsonValue &data = QJsonValue());

    struct Pending
    {
        std::shared_ptr<QPromise<QJsonValue>> promise;
        QTimer *timer = nullptr;
        QString progressToken;
    };

    QPointer<McpTransport> m_transport;
    QAtomicInteger<quint64> m_nextId{1};
    QHash<QString, Pending> m_pending;
    QHash<QString, RequestHandler> m_requestHandlers;
    QHash<QString, NotifyHandler> m_notifyHandlers;
    QHash<QString, ProgressHandler> m_progressHandlers;

    QSet<QString> m_inFlightIncomingIds;
    QSet<QString> m_cancelledIncomingIds;
    QString m_currentProgressToken;
};

} // namespace LLMCore::Mcp
