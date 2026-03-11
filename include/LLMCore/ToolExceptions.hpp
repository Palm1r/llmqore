// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QException>
#include <QString>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore {

class LLMCORE_EXPORT ToolException : public QException
{
public:
    explicit ToolException(const QString &message)
        : m_message(message)
        , m_stdMessage(message.toStdString())
    {}

    void raise() const override { throw *this; }
    ToolException *clone() const override { return new ToolException(*this); }
    const char *what() const noexcept override { return m_stdMessage.c_str(); }

    QString message() const { return m_message; }

private:
    QString m_message;
    std::string m_stdMessage;
};

class LLMCORE_EXPORT ToolRuntimeError : public ToolException
{
public:
    explicit ToolRuntimeError(const QString &message)
        : ToolException(message)
    {}

    void raise() const override { throw *this; }
    ToolRuntimeError *clone() const override { return new ToolRuntimeError(*this); }
};

class LLMCORE_EXPORT ToolInvalidArgument : public ToolException
{
public:
    explicit ToolInvalidArgument(const QString &message)
        : ToolException(message)
    {}

    void raise() const override { throw *this; }
    ToolInvalidArgument *clone() const override { return new ToolInvalidArgument(*this); }
};

} // namespace LLMCore
