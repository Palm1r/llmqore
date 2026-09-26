// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/BaseClient.hpp>

namespace LLMQore {

[[nodiscard]] inline QList<BaseClient::ErrorAnnotation> openAIErrorAnnotations()
{
    return {
        {QStringLiteral("type"), QStringLiteral("type")},
        {QStringLiteral("code"), QStringLiteral("code")}};
}

} // namespace LLMQore
