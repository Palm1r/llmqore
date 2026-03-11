// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/BaseTool.hpp>

namespace LLMCore {

BaseTool::BaseTool(QObject *parent)
    : QObject(parent)
{}

bool BaseTool::isEnabled() const
{
    return m_enabled;
}

void BaseTool::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

} // namespace LLMCore
