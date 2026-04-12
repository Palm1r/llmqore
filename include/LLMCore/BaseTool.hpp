// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QFuture>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <LLMCore/LLMCore_global.h>
#include <LLMCore/ToolResult.hpp>

namespace LLMCore {

class LLMCORE_EXPORT BaseTool : public QObject
{
    Q_OBJECT
public:
    explicit BaseTool(QObject *parent = nullptr);
    ~BaseTool() override = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString description() const = 0;
    virtual QJsonObject parametersSchema() const = 0;

    virtual QFuture<ToolResult> executeAsync(const QJsonObject &input = QJsonObject()) = 0;

    bool isEnabled() const;
    void setEnabled(bool enabled);

private:
    bool m_enabled = true;
};

} // namespace LLMCore
