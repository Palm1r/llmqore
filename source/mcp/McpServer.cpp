// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpServer.hpp>

#include <LLMCore/BasePromptProvider.hpp>
#include <LLMCore/BaseResourceProvider.hpp>
#include <LLMCore/BaseTool.hpp>
#include <LLMCore/Log.hpp>
#include <LLMCore/McpExceptions.hpp>
#include <LLMCore/McpSession.hpp>
#include <LLMCore/McpTransport.hpp>
#include <LLMCore/ToolResult.hpp>
#include <LLMCore/ToolsManager.hpp>

#include <QFuture>
#include <QJsonArray>
#include <QPromise>

#include <optional>

namespace LLMCore::Mcp {

namespace {

QFuture<QJsonValue> makeErrorFuture(const McpRemoteError &err)
{
    QPromise<QJsonValue> promise;
    promise.start();
    promise.setException(std::make_exception_ptr(err));
    promise.finish();
    return promise.future();
}

QFuture<QJsonValue> makeReadyFuture(QJsonValue value)
{
    QPromise<QJsonValue> promise;
    promise.start();
    promise.addResult(std::move(value));
    promise.finish();
    return promise.future();
}

template<typename Provider, typename Item, typename Run>
QFuture<QJsonValue> collectListFromProviders(
    QObject *ctx,
    const QList<QPointer<Provider>> &providers,
    Run runFn,
    QLatin1String resultKey)
{
    QList<QFuture<QList<Item>>> futures;
    for (const auto &providerPtr : providers) {
        if (Provider *p = providerPtr.data())
            futures.append(runFn(p));
    }

    if (futures.isEmpty())
        return makeReadyFuture(QJsonObject{{resultKey, QJsonArray{}}});

    return QtFuture::whenAll(futures.begin(), futures.end())
        .then(ctx, [resultKey](const QList<QFuture<QList<Item>>> &finished) {
            QJsonArray merged;
            for (const QFuture<QList<Item>> &f : finished) {
                try {
                    const QList<Item> list = f.result();
                    for (const Item &item : list)
                        merged.append(item.toJson());
                } catch (...) {
                }
            }
            return QJsonValue(QJsonObject{{resultKey, merged}});
        });
}

template<typename Provider, typename T>
struct TryEachState
{
    QPointer<QObject> ctx;
    QList<QPointer<Provider>> providers;
    std::function<QFuture<T>(Provider *)> runFn;
    std::function<std::optional<QJsonValue>(const T &)> extract;
    QString notFoundMsg;
    std::shared_ptr<QPromise<QJsonValue>> promise;
    int index = 0;
};

template<typename Provider, typename T>
void tryEachAdvance(std::shared_ptr<TryEachState<Provider, T>> s)
{
    while (s->index < s->providers.size() && !s->providers.at(s->index))
        ++s->index;

    if (s->index >= s->providers.size()) {
        s->promise->setException(std::make_exception_ptr(
            McpRemoteError(ErrorCode::InvalidParams, s->notFoundMsg)));
        s->promise->finish();
        return;
    }

    Provider *provider = s->providers.at(s->index).data();
    ++s->index;

    s->runFn(provider)
        .then(s->ctx,
              [s](const T &value) {
                  if (auto json = s->extract(value)) {
                      s->promise->addResult(std::move(*json));
                      s->promise->finish();
                  } else {
                      tryEachAdvance(s);
                  }
              })
        .onFailed(s->ctx, [s](const std::exception &) {
            tryEachAdvance(s);
        });
}

template<typename Provider, typename T, typename Run, typename Extract>
QFuture<QJsonValue> tryEachProvider(
    QObject *ctx,
    QList<QPointer<Provider>> providers,
    Run runFn,
    Extract extract,
    QString notFoundMsg)
{
    auto state = std::make_shared<TryEachState<Provider, T>>();
    state->ctx = ctx;
    state->providers = std::move(providers);
    state->runFn = std::move(runFn);
    state->extract = std::move(extract);
    state->notFoundMsg = std::move(notFoundMsg);
    state->promise = std::make_shared<QPromise<QJsonValue>>();
    state->promise->start();

    tryEachAdvance(state);
    return state->promise->future();
}

} // namespace

McpServer::McpServer(McpTransport *transport, McpServerConfig config, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_session(new McpSession(transport, this))
    , m_config(std::move(config))
{
    installHandlers();
}

McpServer::~McpServer() = default;

void McpServer::installHandlers()
{
    m_session->setRequestHandler(
        QStringLiteral("initialize"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const QString clientVersion = params.value("protocolVersion").toString();
            Implementation clientInfo
                = Implementation::fromJson(params.value("clientInfo").toObject());
            m_clientCapabilities
                = ClientCapabilities::fromJson(params.value("capabilities").toObject());

            ServerCapabilities caps;
            caps.tools = ToolsCapability{/*listChanged*/ true};
            if (!m_resourceProviders.isEmpty()) {
                ResourcesCapability rc;
                rc.listChanged = true;
                for (const auto &rp : m_resourceProviders) {
                    if (rp && rp->supportsSubscription()) {
                        rc.subscribe = true;
                        break;
                    }
                }
                caps.resources = rc;
            }
            if (!m_promptProviders.isEmpty()) {
                caps.prompts = PromptsCapability{/*listChanged*/ true};
            }
            if (m_config.advertiseLogging) {
                caps.logging = LoggingCapability{true};
            }
            if (!m_promptProviders.isEmpty() || !m_resourceProviders.isEmpty()) {
                caps.completions = CompletionsCapability{true};
            }

            QString chosenVersion = QString::fromLatin1(kSupportedProtocolVersion);
            for (const char *known_v : kKnownProtocolVersions) {
                if (clientVersion == QLatin1String(known_v)) {
                    chosenVersion = clientVersion;
                    break;
                }
            }

            InitializeResult result;
            result.protocolVersion = chosenVersion;
            result.capabilities = caps;
            result.serverInfo = m_config.serverInfo;
            result.instructions = m_config.instructions;

            m_initialized = true;
            emit clientInitialized(clientInfo);

            return makeReadyFuture(result.toJson());
        });

    m_session->setNotificationHandler(
        QStringLiteral("notifications/initialized"), [](const QJsonObject &) {});

    m_session->setRequestHandler(
        QStringLiteral("tools/list"),
        [this](const QJsonObject &) -> QFuture<QJsonValue> {
            QJsonArray arr;
            for (LLMCore::BaseTool *tool : collectTools()) {
                if (!tool || !tool->isEnabled())
                    continue;
                ToolInfo info;
                info.name = tool->id();
                info.description = tool->description();
                info.inputSchema = tool->parametersSchema();
                if (tool->displayName() != tool->id())
                    info.title = tool->displayName();
                arr.append(info.toJson());
            }
            QJsonObject result{{"tools", arr}};
            return makeReadyFuture(result);
        });

    m_session->setRequestHandler(
        QStringLiteral("tools/call"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const QString name = params.value("name").toString();
            const QJsonObject args = params.value("arguments").toObject();

            emit toolCallReceived(name);

            LLMCore::BaseTool *tool = findTool(name);
            if (!tool) {
                return makeErrorFuture(McpRemoteError(
                    ErrorCode::MethodNotFound, QString("Tool not found: %1").arg(name)));
            }

            return tool->executeAsync(args)
                .then(this,
                      [](const LLMCore::ToolResult &result) {
                          return QJsonValue(result.toJson());
                      })
                .onFailed(this, [](const std::exception &e) {
                    return QJsonValue(
                        LLMCore::ToolResult::error(QString::fromUtf8(e.what())).toJson());
                });
        });

    m_session->setRequestHandler(
        QStringLiteral("resources/list"),
        [this](const QJsonObject &) -> QFuture<QJsonValue> {
            return collectListFromProviders<BaseResourceProvider, ResourceInfo>(
                this,
                m_resourceProviders,
                [](BaseResourceProvider *p) { return p->listResources(); },
                QLatin1String("resources"));
        });

    m_session->setRequestHandler(
        QStringLiteral("resources/templates/list"),
        [this](const QJsonObject &) -> QFuture<QJsonValue> {
            return collectListFromProviders<BaseResourceProvider, ResourceTemplate>(
                this,
                m_resourceProviders,
                [](BaseResourceProvider *p) { return p->listResourceTemplates(); },
                QLatin1String("resourceTemplates"));
        });

    m_session->setRequestHandler(
        QStringLiteral("resources/read"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const QString uri = params.value("uri").toString();
            emit resourceRead(uri);

            if (m_resourceProviders.isEmpty()) {
                return makeErrorFuture(McpRemoteError(
                    ErrorCode::InvalidParams, QStringLiteral("No resource providers")));
            }

            return tryEachProvider<BaseResourceProvider, ResourceContents>(
                this,
                m_resourceProviders,
                [uri](BaseResourceProvider *p) { return p->readResource(uri); },
                [](const ResourceContents &c) -> std::optional<QJsonValue> {
                    if (c.uri.isEmpty())
                        return std::nullopt;
                    return QJsonValue(QJsonObject{{"contents", QJsonArray{c.toJson()}}});
                },
                QString("Resource not found: %1").arg(uri));
        });

    m_session->setRequestHandler(
        QStringLiteral("resources/subscribe"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const QString uri = params.value("uri").toString();
            for (const auto &providerPtr : m_resourceProviders) {
                BaseResourceProvider *provider = providerPtr.data();
                if (provider && provider->supportsSubscription())
                    provider->subscribe(uri);
            }
            return makeReadyFuture(QJsonObject{});
        });

    m_session->setRequestHandler(
        QStringLiteral("resources/unsubscribe"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const QString uri = params.value("uri").toString();
            for (const auto &providerPtr : m_resourceProviders) {
                BaseResourceProvider *provider = providerPtr.data();
                if (provider && provider->supportsSubscription())
                    provider->unsubscribe(uri);
            }
            return makeReadyFuture(QJsonObject{});
        });

    m_session->setRequestHandler(
        QStringLiteral("prompts/list"),
        [this](const QJsonObject &) -> QFuture<QJsonValue> {
            return collectListFromProviders<BasePromptProvider, PromptInfo>(
                this,
                m_promptProviders,
                [](BasePromptProvider *p) { return p->listPrompts(); },
                QLatin1String("prompts"));
        });

    m_session->setRequestHandler(
        QStringLiteral("prompts/get"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const QString name = params.value("name").toString();
            const QJsonObject args = params.value("arguments").toObject();
            emit promptRequested(name);

            if (m_promptProviders.isEmpty()) {
                return makeErrorFuture(McpRemoteError(
                    ErrorCode::InvalidParams, QStringLiteral("No prompt providers")));
            }

            return tryEachProvider<BasePromptProvider, PromptGetResult>(
                this,
                m_promptProviders,
                [name, args](BasePromptProvider *p) { return p->getPrompt(name, args); },
                [](const PromptGetResult &r) -> std::optional<QJsonValue> {
                    return QJsonValue(r.toJson());
                },
                QString("Prompt not found: %1").arg(name));
        });

    m_session->setRequestHandler(
        QStringLiteral("completion/complete"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const CompletionReference ref
                = CompletionReference::fromJson(params.value("ref").toObject());
            const CompletionArgument arg
                = CompletionArgument::fromJson(params.value("argument").toObject());
            const QJsonObject ctxArguments
                = params.value("context").toObject().value("arguments").toObject();

            const auto mergeCompletions = [this](auto &providers, auto runFn) -> QFuture<QJsonValue> {
                QList<QFuture<CompletionResult>> futures;
                for (const auto &providerPtr : providers) {
                    if (auto *p = providerPtr.data())
                        futures.append(runFn(p));
                }
                if (futures.isEmpty())
                    return makeReadyFuture(CompletionResult{}.toJson());

                return QtFuture::whenAll(futures.begin(), futures.end())
                    .then(this, [](const QList<QFuture<CompletionResult>> &finished) {
                        CompletionResult merged;
                        for (const QFuture<CompletionResult> &f : finished) {
                            try {
                                const CompletionResult r = f.result();
                                for (const QString &v : r.values)
                                    merged.values.append(v);
                                if (r.hasMore)
                                    merged.hasMore = true;
                                if (r.total.has_value())
                                    merged.total = merged.total.value_or(0) + *r.total;
                            } catch (...) {
                            }
                        }
                        return QJsonValue(merged.toJson());
                    });
            };

            if (ref.type == QLatin1String("ref/prompt")) {
                if (m_promptProviders.isEmpty() || ref.name.isEmpty())
                    return makeReadyFuture(CompletionResult{}.toJson());
                return mergeCompletions(
                    m_promptProviders, [&](BasePromptProvider *p) {
                        return p->completeArgument(ref.name, arg.name, arg.value, ctxArguments);
                    });
            }

            if (ref.type == QLatin1String("ref/resource")) {
                if (m_resourceProviders.isEmpty() || ref.uri.isEmpty())
                    return makeReadyFuture(CompletionResult{}.toJson());
                return mergeCompletions(
                    m_resourceProviders, [&](BaseResourceProvider *p) {
                        return p->completeArgument(ref.uri, arg.name, arg.value, ctxArguments);
                    });
            }

            return makeReadyFuture(CompletionResult{}.toJson());
        });

    m_session->setRequestHandler(
        QStringLiteral("logging/setLevel"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            const QString level = params.value("level").toString();
            if (level.isEmpty()) {
                return makeErrorFuture(McpRemoteError(
                    ErrorCode::InvalidParams, QStringLiteral("Missing 'level'")));
            }
            m_logLevel = level;
            emit logLevelChanged(level);
            return makeReadyFuture(QJsonObject{});
        });

    m_session->setRequestHandler(
        QStringLiteral("ping"),
        [](const QJsonObject &) -> QFuture<QJsonValue> { return makeReadyFuture(QJsonObject{}); });
}

void McpServer::setToolsManager(LLMCore::ToolsManager *manager)
{
    if (m_toolsManager) {
        disconnect(m_toolsManager, nullptr, this, nullptr);
    }
    m_toolsManager = manager;
}

void McpServer::addTool(LLMCore::BaseTool *tool)
{
    if (!tool)
        return;
    m_standaloneTools.insert(tool->id(), tool);
    if (m_initialized)
        m_session->sendNotification(QStringLiteral("notifications/tools/list_changed"));
}

void McpServer::removeTool(const QString &name)
{
    if (m_standaloneTools.remove(name) && m_initialized) {
        m_session->sendNotification(QStringLiteral("notifications/tools/list_changed"));
    }
}

void McpServer::addResourceProvider(BaseResourceProvider *provider)
{
    if (!provider)
        return;
    m_resourceProviders.append(provider);
    connect(provider, &BaseResourceProvider::listChanged, this, [this]() {
        if (m_initialized)
            m_session->sendNotification(QStringLiteral("notifications/resources/list_changed"));
    });
    connect(provider, &BaseResourceProvider::resourceUpdated, this, [this](const QString &uri) {
        if (m_initialized) {
            m_session->sendNotification(
                QStringLiteral("notifications/resources/updated"),
                QJsonObject{{"uri", uri}});
        }
    });
}

void McpServer::removeResourceProvider(BaseResourceProvider *provider)
{
    m_resourceProviders.removeAll(provider);
    if (provider)
        disconnect(provider, nullptr, this, nullptr);
}

void McpServer::addPromptProvider(BasePromptProvider *provider)
{
    if (!provider)
        return;
    m_promptProviders.append(provider);
    connect(provider, &BasePromptProvider::listChanged, this, [this]() {
        if (m_initialized)
            m_session->sendNotification(QStringLiteral("notifications/prompts/list_changed"));
    });
}

void McpServer::removePromptProvider(BasePromptProvider *provider)
{
    m_promptProviders.removeAll(provider);
    if (provider)
        disconnect(provider, nullptr, this, nullptr);
}

QFuture<CreateMessageResult> McpServer::createSamplingMessage(
    const CreateMessageParams &params, std::chrono::milliseconds timeout)
{
    if (!m_initialized) {
        QPromise<CreateMessageResult> p;
        p.start();
        p.setException(std::make_exception_ptr(
            McpProtocolError(QStringLiteral("Server not initialized"))));
        p.finish();
        return p.future();
    }
    if (!m_clientCapabilities.sampling.has_value()) {
        QPromise<CreateMessageResult> p;
        p.start();
        p.setException(std::make_exception_ptr(McpProtocolError(
            QStringLiteral("Client did not advertise the `sampling` capability"))));
        p.finish();
        return p.future();
    }

    return m_session
        ->sendRequest(QStringLiteral("sampling/createMessage"), params.toJson(), timeout)
        .then(this, [](const QJsonValue &value) {
            return CreateMessageResult::fromJson(value.toObject());
        });
}

QFuture<ElicitResult> McpServer::createElicitation(
    const ElicitRequestParams &params, std::chrono::milliseconds timeout)
{
    if (!m_initialized) {
        QPromise<ElicitResult> p;
        p.start();
        p.setException(std::make_exception_ptr(
            McpProtocolError(QStringLiteral("Server not initialized"))));
        p.finish();
        return p.future();
    }
    if (!m_clientCapabilities.elicitation.has_value()) {
        QPromise<ElicitResult> p;
        p.start();
        p.setException(std::make_exception_ptr(McpProtocolError(
            QStringLiteral("Client did not advertise the `elicitation` capability"))));
        p.finish();
        return p.future();
    }

    return m_session
        ->sendRequest(QStringLiteral("elicitation/create"), params.toJson(), timeout)
        .then(this, [](const QJsonValue &value) {
            return ElicitResult::fromJson(value.toObject());
        });
}

void McpServer::sendLogMessage(
    const QString &level,
    const QString &logger,
    const QJsonValue &data,
    const QString &message)
{
    if (!m_initialized)
        return;
    QJsonObject params{
        {"level", level},
        {"data", data},
    };
    if (!logger.isEmpty())
        params.insert("logger", logger);
    if (!message.isEmpty())
        params.insert("message", message);
    m_session->sendNotification(QStringLiteral("notifications/message"), params);
}

void McpServer::start()
{
    if (!m_transport)
        return;
    if (!m_transport->isOpen())
        m_transport->start();
}

void McpServer::stop()
{
    if (m_transport && m_transport->isOpen())
        m_transport->stop();
    m_initialized = false;
}

QList<LLMCore::BaseTool *> McpServer::collectTools() const
{
    QList<LLMCore::BaseTool *> tools;
    if (m_toolsManager)
        tools = m_toolsManager->registeredTools();
    for (const auto &t : m_standaloneTools) {
        if (t)
            tools.append(t.data());
    }
    return tools;
}

LLMCore::BaseTool *McpServer::findTool(const QString &name) const
{
    if (m_toolsManager) {
        if (auto *t = m_toolsManager->tool(name))
            return t;
    }
    auto it = m_standaloneTools.constFind(name);
    if (it != m_standaloneTools.constEnd())
        return it->data();
    return nullptr;
}

} // namespace LLMCore::Mcp
