// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QException>
#include <QJsonValue>
#include <QString>

#include <LLMQore/LLMQore_global.h>

namespace LLMQore::Rpc {

namespace ErrorCode {
inline constexpr int ParseError           = -32700;
inline constexpr int InvalidRequest       = -32600;
inline constexpr int MethodNotFound       = -32601;
inline constexpr int InvalidParams        = -32602;
inline constexpr int InternalError        = -32603;
inline constexpr int ServerNotInitialized = -32002;
inline constexpr int RequestCancelled     = -32800;
inline constexpr int ContentTooLarge      = -32801;
} // namespace ErrorCode

class LLMQORE_EXPORT JsonRpcException : public QException
{
public:
    explicit JsonRpcException(const QString &message)
        : m_message(message)
        , m_stdMessage(message.toStdString())
    {}

    void raise() const override { throw *this; }
    JsonRpcException *clone() const override { return new JsonRpcException(*this); }
    const char *what() const noexcept override { return m_stdMessage.c_str(); }

    QString message() const { return m_message; }

private:
    QString m_message;
    std::string m_stdMessage;
};

class LLMQORE_EXPORT TransportError : public JsonRpcException
{
public:
    explicit TransportError(const QString &message)
        : JsonRpcException(message)
    {}

    void raise() const override { throw *this; }
    TransportError *clone() const override { return new TransportError(*this); }
};

class LLMQORE_EXPORT ProtocolError : public JsonRpcException
{
public:
    explicit ProtocolError(const QString &message)
        : JsonRpcException(message)
    {}

    void raise() const override { throw *this; }
    ProtocolError *clone() const override { return new ProtocolError(*this); }
};

class LLMQORE_EXPORT TimeoutError : public JsonRpcException
{
public:
    explicit TimeoutError(const QString &message)
        : JsonRpcException(message)
    {}

    void raise() const override { throw *this; }
    TimeoutError *clone() const override { return new TimeoutError(*this); }
};

class LLMQORE_EXPORT CancelledError : public JsonRpcException
{
public:
    explicit CancelledError(const QString &message = QStringLiteral("Request cancelled"))
        : JsonRpcException(message)
    {}

    void raise() const override { throw *this; }
    CancelledError *clone() const override { return new CancelledError(*this); }
};

class LLMQORE_EXPORT RemoteError : public JsonRpcException
{
public:
    RemoteError(int code, const QString &message, const QJsonValue &data = QJsonValue())
        : JsonRpcException(QString("JSON-RPC error %1: %2").arg(code).arg(message))
        , m_code(code)
        , m_remoteMessage(message)
        , m_data(data)
    {}

    void raise() const override { throw *this; }
    RemoteError *clone() const override { return new RemoteError(*this); }

    int code() const { return m_code; }
    QString remoteMessage() const { return m_remoteMessage; }
    QJsonValue data() const { return m_data; }

private:
    int m_code;
    QString m_remoteMessage;
    QJsonValue m_data;
};

} // namespace LLMQore::Rpc
