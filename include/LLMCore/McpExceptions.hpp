// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QException>
#include <QJsonValue>
#include <QString>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore::Mcp {

class LLMCORE_EXPORT McpException : public QException
{
public:
    explicit McpException(const QString &message)
        : m_message(message)
        , m_stdMessage(message.toStdString())
    {}

    void raise() const override { throw *this; }
    McpException *clone() const override { return new McpException(*this); }
    const char *what() const noexcept override { return m_stdMessage.c_str(); }

    QString message() const { return m_message; }

private:
    QString m_message;
    std::string m_stdMessage;
};

class LLMCORE_EXPORT McpTransportError : public McpException
{
public:
    explicit McpTransportError(const QString &message)
        : McpException(message)
    {}

    void raise() const override { throw *this; }
    McpTransportError *clone() const override { return new McpTransportError(*this); }
};

class LLMCORE_EXPORT McpProtocolError : public McpException
{
public:
    explicit McpProtocolError(const QString &message)
        : McpException(message)
    {}

    void raise() const override { throw *this; }
    McpProtocolError *clone() const override { return new McpProtocolError(*this); }
};

class LLMCORE_EXPORT McpTimeoutError : public McpException
{
public:
    explicit McpTimeoutError(const QString &message)
        : McpException(message)
    {}

    void raise() const override { throw *this; }
    McpTimeoutError *clone() const override { return new McpTimeoutError(*this); }
};

class LLMCORE_EXPORT McpCancelledError : public McpException
{
public:
    explicit McpCancelledError(const QString &message = QStringLiteral("Request cancelled"))
        : McpException(message)
    {}

    void raise() const override { throw *this; }
    McpCancelledError *clone() const override { return new McpCancelledError(*this); }
};

class LLMCORE_EXPORT McpRemoteError : public McpException
{
public:
    McpRemoteError(int code, const QString &message, const QJsonValue &data = QJsonValue())
        : McpException(QString("MCP error %1: %2").arg(code).arg(message))
        , m_code(code)
        , m_remoteMessage(message)
        , m_data(data)
    {}

    void raise() const override { throw *this; }
    McpRemoteError *clone() const override { return new McpRemoteError(*this); }

    int code() const { return m_code; }
    QString remoteMessage() const { return m_remoteMessage; }
    QJsonValue data() const { return m_data; }

private:
    int m_code;
    QString m_remoteMessage;
    QJsonValue m_data;
};

} // namespace LLMCore::Mcp
