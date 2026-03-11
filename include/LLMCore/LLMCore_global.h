// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QtGlobal>

#if defined(LLMCORE_STATIC_LIB)
#define LLMCORE_EXPORT
#elif defined(LLMCORE_LIBRARY)
#define LLMCORE_EXPORT Q_DECL_EXPORT
#else
#define LLMCORE_EXPORT Q_DECL_IMPORT
#endif
