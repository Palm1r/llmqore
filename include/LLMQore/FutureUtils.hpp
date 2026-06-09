#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>

#include <QPromise>

#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/McpExceptions.hpp>

namespace LLMQore {

template<typename T, typename F>
auto futureThen(QObject *context, const QFuture<T> &future, F &&fn)
    -> QFuture<std::decay_t<std::invoke_result_t<F, const T &>>>;

template<typename F>
auto futureThen(QObject *context, const QFuture<void> &future, F &&fn)
    -> QFuture<std::decay_t<std::invoke_result_t<F>>>;

template<typename T, typename F>
QFuture<T> futureOnFailed(QObject *context, const QFuture<T> &future, F &&fn);

namespace detail {

template<typename T>
struct FutureValue;

template<typename T>
struct IsQFuture : std::false_type {};

template<typename T>
struct FutureValue<QFuture<T>>
{
    using type = T;
};

template<typename T>
struct IsQFuture<QFuture<T>> : std::true_type {};

template<typename T>
T futureResultOrRethrow(const QFuture<T> &future)
{
    return future.result();
}

inline void futureResultOrRethrow(QFuture<void> future)
{
    future.waitForFinished();
}

template<typename T>
QFuture<T> futureUnwrap(const QFuture<QFuture<T>> &future)
{
    auto promise = std::make_shared<QPromise<T>>();
    promise->start();

    auto *watcher = new QFutureWatcher<QFuture<T>>();
    QObject::connect(watcher, &QFutureWatcher<QFuture<T>>::finished, watcher,
                     [watcher, promise]() mutable {
                         try {
                             const QFuture<T> inner = futureResultOrRethrow(watcher->future());
                             auto *innerWatcher = new QFutureWatcher<T>();
                             QObject::connect(innerWatcher, &QFutureWatcher<T>::finished, innerWatcher,
                                              [innerWatcher, promise]() mutable {
                                                  try {
                                                      if constexpr (std::is_void_v<T>) {
                                                          futureResultOrRethrow(innerWatcher->future());
                                                          promise->finish();
                                                      } else {
                                                          promise->addResult(
                                                              futureResultOrRethrow(innerWatcher->future()));
                                                          promise->finish();
                                                      }
                                                  } catch (...) {
                                                      promise->setException(std::current_exception());
                                                      promise->finish();
                                                  }
                                                  innerWatcher->deleteLater();
                                              });
                             innerWatcher->setFuture(inner);
                         } catch (...) {
                             promise->setException(std::current_exception());
                             promise->finish();
                         }
                         watcher->deleteLater();
                     });
    watcher->setFuture(future);
    return promise->future();
}

template<typename F, typename E, typename T>
bool invokeFailureHandler(
    const std::shared_ptr<QPromise<T>> &promise,
    F &&fn,
    const E &error)
{
    if constexpr (std::is_invocable_v<F, const E &>) {
        try {
            using Result = std::invoke_result_t<F, const E &>;
            if constexpr (std::is_void_v<Result>) {
                std::forward<F>(fn)(error);
            } else {
                if constexpr (std::is_void_v<T>)
                    std::forward<F>(fn)(error);
                else
                    promise->addResult(std::forward<F>(fn)(error));
            }
            promise->finish();
        } catch (...) {
            promise->setException(std::current_exception());
            promise->finish();
        }
        return true;
    }
    return false;
}

template<typename F, typename T>
void resolveFailure(
    const std::shared_ptr<QPromise<T>> &promise,
    F &&fn,
    std::exception_ptr exception)
{
    try {
        std::rethrow_exception(exception);
    } catch (const HttpTransportError &e) {
        if (invokeFailureHandler(promise, fn, e))
            return;
    } catch (const Mcp::McpException &e) {
        if (invokeFailureHandler(promise, fn, e))
            return;
    } catch (const std::exception &e) {
        if (invokeFailureHandler(promise, fn, e))
            return;
    }

    promise->setException(exception);
    promise->finish();
}

template<typename T>
class FutureCompat
{
public:
    explicit FutureCompat(QFuture<T> future)
        : m_future(std::move(future))
    {}

    template<typename F>
    auto then(QObject *context, F &&fn) const
        -> FutureCompat<std::decay_t<std::invoke_result_t<F, const T &>>>
    {
        return FutureCompat<std::decay_t<std::invoke_result_t<F, const T &>>>(
            LLMQore::futureThen(context, m_future, std::forward<F>(fn)));
    }

    template<typename F>
    FutureCompat<T> onFailed(QObject *context, F &&fn) const
    {
        return FutureCompat<T>(LLMQore::futureOnFailed(context, m_future, std::forward<F>(fn)));
    }

    template<typename U = T, std::enable_if_t<IsQFuture<U>::value, int> = 0>
    auto unwrap() const -> FutureCompat<typename FutureValue<U>::type>
    {
        using Inner = typename FutureValue<U>::type;
        return FutureCompat<Inner>(LLMQore::detail::futureUnwrap(m_future));
    }

    operator QFuture<T>() const
    {
        return m_future;
    }

private:
    QFuture<T> m_future;
};

template<>
class FutureCompat<void>
{
public:
    explicit FutureCompat(QFuture<void> future)
        : m_future(std::move(future))
    {}

    template<typename F>
    auto then(QObject *context, F &&fn) const
        -> FutureCompat<std::decay_t<std::invoke_result_t<F>>>
    {
        return FutureCompat<std::decay_t<std::invoke_result_t<F>>>(
            LLMQore::futureThen(context, m_future, std::forward<F>(fn)));
    }

    template<typename F>
    FutureCompat<void> onFailed(QObject *context, F &&fn) const
    {
        return FutureCompat<void>(LLMQore::futureOnFailed(context, m_future, std::forward<F>(fn)));
    }

    operator QFuture<void>() const
    {
        return m_future;
    }

private:
    QFuture<void> m_future;
};

} // namespace detail

template<typename T, typename F>
auto futureThen(QObject *context, const QFuture<T> &future, F &&fn)
    -> QFuture<std::decay_t<std::invoke_result_t<F, const T &>>>
{
    using Result = std::decay_t<std::invoke_result_t<F, const T &>>;
    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();

    auto *watcher = new QFutureWatcher<T>(context);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, watcher,
                     [watcher, promise, fn = std::forward<F>(fn)]() mutable {
                         try {
                             if constexpr (std::is_void_v<Result>) {
                                 fn(detail::futureResultOrRethrow(watcher->future()));
                                 promise->finish();
                             } else {
                                 promise->addResult(
                                     fn(detail::futureResultOrRethrow(watcher->future())));
                                 promise->finish();
                             }
                         } catch (...) {
                             promise->setException(std::current_exception());
                             promise->finish();
                         }
                         watcher->deleteLater();
                     });
    watcher->setFuture(future);
    return promise->future();
}

template<typename F>
auto futureThen(QObject *context, const QFuture<void> &future, F &&fn)
    -> QFuture<std::decay_t<std::invoke_result_t<F>>>
{
    using Result = std::decay_t<std::invoke_result_t<F>>;
    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();

    auto *watcher = new QFutureWatcher<void>(context);
    QObject::connect(watcher, &QFutureWatcher<void>::finished, watcher,
                     [watcher, promise, fn = std::forward<F>(fn)]() mutable {
                         try {
                             detail::futureResultOrRethrow(watcher->future());
                             if constexpr (std::is_void_v<Result>) {
                                 fn();
                                 promise->finish();
                             } else {
                                 promise->addResult(fn());
                                 promise->finish();
                             }
                         } catch (...) {
                             promise->setException(std::current_exception());
                             promise->finish();
                         }
                         watcher->deleteLater();
                     });
    watcher->setFuture(future);
    return promise->future();
}

template<typename T, typename F>
QFuture<T> futureOnFailed(QObject *context, const QFuture<T> &future, F &&fn)
{
    auto promise = std::make_shared<QPromise<T>>();
    promise->start();

    auto *watcher = new QFutureWatcher<T>(context);
    QObject::connect(watcher, &QFutureWatcher<T>::finished, watcher,
                     [watcher, promise, fn = std::forward<F>(fn)]() mutable {
                         try {
                             if constexpr (std::is_void_v<T>) {
                                 detail::futureResultOrRethrow(watcher->future());
                                 promise->finish();
                             } else {
                                 promise->addResult(detail::futureResultOrRethrow(watcher->future()));
                                 promise->finish();
                             }
                         } catch (...) {
                             detail::resolveFailure(promise, fn, std::current_exception());
                         }
                         watcher->deleteLater();
                     });
    watcher->setFuture(future);
    return promise->future();
}

template<typename T>
detail::FutureCompat<std::decay_t<T>> compat(QFuture<T> future)
{
    return detail::FutureCompat<std::decay_t<T>>(std::move(future));
}

} // namespace LLMQore
