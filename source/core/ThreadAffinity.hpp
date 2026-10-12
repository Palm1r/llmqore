// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QThread>

namespace LLMQore {

inline void assertOwningThread(const QObject *self, const char *what)
{
    Q_UNUSED(self)
    Q_UNUSED(what)
    Q_ASSERT_X(self->thread() == QThread::currentThread(), what,
               "object accessed from a thread other than the one that owns it");
}

inline void assertSameThread(const QObject *self, const QObject *other, const char *what)
{
    Q_UNUSED(self)
    Q_UNUSED(other)
    Q_UNUSED(what)
    Q_ASSERT_X(!other || other->thread() == self->thread(), what,
               "a provider must live in the same thread as the object it is handed to");
}

} // namespace LLMQore

#define LLMQORE_ASSERT_OWNING_THREAD() ::LLMQore::assertOwningThread(this, Q_FUNC_INFO)
#define LLMQORE_ASSERT_SAME_THREAD(other) ::LLMQore::assertSameThread(this, other, Q_FUNC_INFO)
