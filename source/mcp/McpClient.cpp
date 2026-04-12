// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMCore/McpClient.hpp>

#include <LLMCore/BaseClient.hpp>
#include <LLMCore/BaseElicitationProvider.hpp>
#include <LLMCore/BaseRootsProvider.hpp>
#include <LLMCore/Log.hpp>
#include <LLMCore/McpExceptions.hpp>
#include <LLMCore/McpSession.hpp>
#include <LLMCore/McpTransport.hpp>

#include <QJsonArray>
#include <QPromise>

namespace LLMCore::Mcp {

namespace {

QFuture<QJsonValue> makeErrorFuture(McpException err)
{
    QPromise<QJsonValue> promise;
    promise.start();
    promise.setException(std::make_exception_ptr(std::move(err)));
    promise.finish();
    return promise.future();
}

QFuture<QJsonValue> makeReadyJsonFuture(QJsonValue value)
{
    QPromise<QJsonValue> promise;
    promise.start();
    promise.addResult(std::move(value));
    promise.finish();
    return promise.future();
}

} // namespace

McpClient::McpClient(McpTransport *transport, Implementation clientInfo, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_session(new McpSession(transport, this))
    , m_clientInfo(std::move(clientInfo))
{
    if (m_transport) {
        connect(
            m_transport,
            &McpTransport::closed,
            this,
            [this]() {
                m_initialized = false;
                emit disconnected();
            });
        connect(
            m_transport,
            &McpTransport::errorOccurred,
            this,
            &McpClient::errorOccurred);
    }

    installHandlers();
}

McpClient::~McpClient() = default;

void McpClient::installHandlers()
{
    m_session->setNotificationHandler(
        QStringLiteral("notifications/tools/list_changed"),
        [this](const QJsonObject &) { emit toolsChanged(); });
    m_session->setNotificationHandler(
        QStringLiteral("notifications/resources/list_changed"),
        [this](const QJsonObject &) { emit resourcesChanged(); });
    m_session->setNotificationHandler(
        QStringLiteral("notifications/resources/updated"),
        [this](const QJsonObject &params) {
            emit resourceUpdated(params.value("uri").toString());
        });
    m_session->setNotificationHandler(
        QStringLiteral("notifications/prompts/list_changed"),
        [this](const QJsonObject &) { emit promptsChanged(); });
    m_session->setNotificationHandler(
        QStringLiteral("notifications/message"), [this](const QJsonObject &params) {
            emit logMessage(
                params.value("level").toString(),
                params.value("logger").toString(),
                params.value("data"),
                params.value("message").toString());
        });

    m_session->setRequestHandler(
        QStringLiteral("roots/list"),
        [this](const QJsonObject &) -> QFuture<QJsonValue> {
            if (!m_rootsProvider)
                return makeReadyJsonFuture(QJsonObject{{"roots", QJsonArray{}}});

            return m_rootsProvider->listRoots()
                .then(this,
                      [](const QList<Root> &list) {
                          QJsonArray arr;
                          for (const Root &r : list)
                              arr.append(r.toJson());
                          return QJsonValue(QJsonObject{{"roots", arr}});
                      })
                .onFailed(this, [](const std::exception &) {
                    return QJsonValue(QJsonObject{{"roots", QJsonArray{}}});
                });
        });

    m_session->setRequestHandler(
        QStringLiteral("ping"), [](const QJsonObject &) -> QFuture<QJsonValue> {
            return makeReadyJsonFuture(QJsonObject{});
        });

    m_session->setRequestHandler(
        QStringLiteral("sampling/createMessage"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (!m_samplingClient || !m_samplingBuilder) {
                throw McpRemoteError(
                    ErrorCode::MethodNotFound,
                    QStringLiteral("sampling/createMessage not supported"));
            }

            const CreateMessageParams req = CreateMessageParams::fromJson(params);
            QJsonObject payload;
            try {
                payload = m_samplingBuilder(req);
            } catch (const std::exception &e) {
                throw McpRemoteError(
                    ErrorCode::InvalidParams,
                    QString::fromUtf8(e.what()));
            }

            auto promise = std::make_shared<QPromise<QJsonValue>>();
            promise->start();

            LLMCore::RequestCallbacks cbs;
            cbs.onFinalized = [promise](
                                  const LLMCore::RequestID &,
                                  const LLMCore::CompletionInfo &info) {
                CreateMessageResult r;
                r.role = QStringLiteral("assistant");
                r.content = QJsonObject{
                    {"type", "text"},
                    {"text", info.fullText},
                };
                r.model = info.model;
                r.stopReason = info.stopReason;
                promise->addResult(QJsonValue(r.toJson()));
                promise->finish();
            };
            cbs.onFailed = [promise](
                               const LLMCore::RequestID &, const QString &err) {
                promise->setException(std::make_exception_ptr(
                    McpRemoteError(ErrorCode::InternalError, err)));
                promise->finish();
            };

            m_samplingClient->sendMessage(payload, std::move(cbs));
            return promise->future();
        });

    m_session->setRequestHandler(
        QStringLiteral("elicitation/create"),
        [this](const QJsonObject &params) -> QFuture<QJsonValue> {
            if (!m_elicitationProvider) {
                throw McpRemoteError(
                    ErrorCode::MethodNotFound,
                    QStringLiteral("elicitation/create not supported"));
            }
            const ElicitRequestParams req = ElicitRequestParams::fromJson(params);
            return m_elicitationProvider->elicit(req).then(
                this,
                [](const ElicitResult &result) {
                    return QJsonValue(result.toJson());
                });
        });
}

QFuture<InitializeResult> McpClient::connectAndInitialize(std::chrono::milliseconds timeout)
{
    if (!m_transport) {
        return makeErrorFuture(McpTransportError(QStringLiteral("No transport")))
            .then(this, [](const QJsonValue &) { return InitializeResult{}; });
    }

    if (!m_transport->isOpen())
        m_transport->start();

    ClientCapabilities clientCaps;
    if (m_rootsProvider)
        clientCaps.roots = RootsCapability{/*listChanged*/ true};
    if (m_samplingClient && m_samplingBuilder)
        clientCaps.sampling = SamplingCapability{true};
    if (m_elicitationProvider)
        clientCaps.elicitation = ElicitationCapability{true};

    const QJsonObject params{
        {"protocolVersion", QString::fromLatin1(kSupportedProtocolVersion)},
        {"capabilities", clientCaps.toJson()},
        {"clientInfo", m_clientInfo.toJson()},
    };

    return m_session->sendRequest(QStringLiteral("initialize"), params, timeout)
        .then(this,
              [this](const QJsonValue &result) {
                  m_initResult = InitializeResult::fromJson(result.toObject());
                  m_initialized = true;

                  const QString v = m_initResult.protocolVersion;
                  bool known = false;
                  for (const char *knownV : kKnownProtocolVersions) {
                      if (v == QLatin1String(knownV)) {
                          known = true;
                          break;
                      }
                  }
                  if (!known) {
                      qCWarning(llmMcpLog).noquote()
                          << QString("Unexpected protocol version from server: %1").arg(v);
                  }

                  m_session->sendNotification(QStringLiteral("notifications/initialized"));
                  emit initialized(m_initResult);
                  return m_initResult;
              })
        .onFailed(this,
                  [this](const McpException &e) -> InitializeResult {
                      emit errorOccurred(e.message());
                      e.raise();
                      Q_UNREACHABLE_RETURN(InitializeResult{});
                  })
        .onFailed(this,
                  [this](const std::exception &e) -> InitializeResult {
                      const QString msg = QString::fromUtf8(e.what());
                      emit errorOccurred(msg);
                      throw McpException(msg);
                  });
}

QFuture<QJsonValue> McpClient::sendInitialized(
    const QString &method, const QJsonObject &params)
{
    if (!m_initialized)
        return makeErrorFuture(McpProtocolError(QStringLiteral("Client not initialized")));
    return m_session->sendRequest(method, params);
}

QFuture<void> McpClient::ping(std::chrono::milliseconds timeout)
{
    QFuture<QJsonValue> raw = (!m_transport || !m_transport->isOpen())
        ? makeErrorFuture(McpTransportError(QStringLiteral("Transport is not open")))
        : m_session->sendRequest(QStringLiteral("ping"), QJsonObject{}, timeout);
    return raw.then(this, [](const QJsonValue &) {});
}

QFuture<void> McpClient::setLogLevel(const QString &level)
{
    return sendInitialized(
               QStringLiteral("logging/setLevel"), QJsonObject{{"level", level}})
        .then(this, [](const QJsonValue &) {});
}

QFuture<QList<ToolInfo>> McpClient::listTools()
{
    return sendInitialized(QStringLiteral("tools/list"))
        .then(this, [this](const QJsonValue &result) {
            QList<ToolInfo> tools;
            const QJsonArray arr = result.toObject().value("tools").toArray();
            for (const QJsonValue &item : arr)
                tools.append(ToolInfo::fromJson(item.toObject()));
            m_cachedTools = tools;
            return tools;
        });
}

QFuture<LLMCore::ToolResult> McpClient::callTool(
    const QString &name, const QJsonObject &arguments)
{
    return sendInitialized(
               QStringLiteral("tools/call"),
               QJsonObject{{"name", name}, {"arguments", arguments}})
        .then(this, [](const QJsonValue &result) {
            return LLMCore::ToolResult::fromJson(result.toObject());
        });
}

McpClient::CancellableToolCall McpClient::callToolWithProgress(
    const QString &name, const QJsonObject &arguments, ProgressCallback onProgress)
{
    CancellableToolCall out;

    if (!m_initialized) {
        auto p = std::make_shared<QPromise<LLMCore::ToolResult>>();
        p->start();
        p->setException(std::make_exception_ptr(
            McpProtocolError(QStringLiteral("Client not initialized"))));
        p->finish();
        out.future = p->future();
        return out;
    }

    QJsonObject params{{"name", name}, {"arguments", arguments}};
    auto cancellable
        = m_session->sendCancellableRequest(QStringLiteral("tools/call"), params);
    out.requestId = cancellable.requestId;
    out.progressToken = cancellable.requestId;

    if (onProgress) {
        m_session->setProgressHandler(
            cancellable.requestId,
            [onProgress](double progress, double total, const QString &message) {
                onProgress(progress, total, message);
            });
    }

    out.future = cancellable.future.then(this, [](const QJsonValue &result) {
        return LLMCore::ToolResult::fromJson(result.toObject());
    });
    return out;
}

void McpClient::cancel(const QString &requestId, const QString &reason)
{
    if (m_session)
        m_session->cancelRequest(requestId, reason);
}

QFuture<QList<ResourceInfo>> McpClient::listResources()
{
    return sendInitialized(QStringLiteral("resources/list"))
        .then(this, [](const QJsonValue &result) {
            QList<ResourceInfo> resources;
            const QJsonArray arr = result.toObject().value("resources").toArray();
            for (const QJsonValue &item : arr)
                resources.append(ResourceInfo::fromJson(item.toObject()));
            return resources;
        });
}

QFuture<QList<ResourceTemplate>> McpClient::listResourceTemplates()
{
    return sendInitialized(QStringLiteral("resources/templates/list"))
        .then(this, [](const QJsonValue &result) {
            QList<ResourceTemplate> templates;
            const QJsonArray arr = result.toObject().value("resourceTemplates").toArray();
            for (const QJsonValue &item : arr)
                templates.append(ResourceTemplate::fromJson(item.toObject()));
            return templates;
        });
}

QFuture<ResourceContents> McpClient::readResource(const QString &uri)
{
    return sendInitialized(QStringLiteral("resources/read"), QJsonObject{{"uri", uri}})
        .then(this, [](const QJsonValue &result) {
            const QJsonObject obj = result.toObject();
            const QJsonArray contents = obj.value("contents").toArray();
            if (contents.isEmpty())
                return ResourceContents{};
            return ResourceContents::fromJson(contents.first().toObject());
        });
}

QFuture<void> McpClient::subscribeResource(const QString &uri)
{
    return sendInitialized(QStringLiteral("resources/subscribe"), QJsonObject{{"uri", uri}})
        .then(this, [](const QJsonValue &) {});
}

QFuture<void> McpClient::unsubscribeResource(const QString &uri)
{
    return sendInitialized(QStringLiteral("resources/unsubscribe"), QJsonObject{{"uri", uri}})
        .then(this, [](const QJsonValue &) {});
}

QFuture<QList<PromptInfo>> McpClient::listPrompts()
{
    return sendInitialized(QStringLiteral("prompts/list"))
        .then(this, [](const QJsonValue &result) {
            QList<PromptInfo> prompts;
            const QJsonArray arr = result.toObject().value("prompts").toArray();
            for (const QJsonValue &item : arr)
                prompts.append(PromptInfo::fromJson(item.toObject()));
            return prompts;
        });
}

QFuture<PromptGetResult> McpClient::getPrompt(
    const QString &name, const QJsonObject &arguments)
{
    QJsonObject params{{"name", name}};
    if (!arguments.isEmpty())
        params.insert("arguments", arguments);

    return sendInitialized(QStringLiteral("prompts/get"), params)
        .then(this, [](const QJsonValue &result) {
            return PromptGetResult::fromJson(result.toObject());
        });
}

QFuture<CompletionResult> McpClient::complete(
    const CompletionReference &ref,
    const QString &argumentName,
    const QString &partialValue,
    const QJsonObject &contextArguments)
{
    QJsonObject params{
        {"ref", ref.toJson()},
        {"argument", QJsonObject{{"name", argumentName}, {"value", partialValue}}},
    };
    if (!contextArguments.isEmpty())
        params.insert("context", QJsonObject{{"arguments", contextArguments}});

    return sendInitialized(QStringLiteral("completion/complete"), params)
        .then(this, [](const QJsonValue &result) {
            return CompletionResult::fromJson(result.toObject());
        });
}

void McpClient::setSamplingClient(
    LLMCore::BaseClient *client, SamplingPayloadBuilder builder)
{
    m_samplingClient = client;
    m_samplingBuilder = std::move(builder);
}

void McpClient::setElicitationProvider(BaseElicitationProvider *provider)
{
    m_elicitationProvider = provider;
}

void McpClient::setRootsProvider(BaseRootsProvider *provider)
{
    if (m_rootsProvider) {
        disconnect(m_rootsProvider, nullptr, this, nullptr);
    }
    m_rootsProvider = provider;
    if (provider) {
        connect(provider, &BaseRootsProvider::listChanged, this, [this]() {
            if (m_initialized)
                m_session->sendNotification(
                    QStringLiteral("notifications/roots/list_changed"));
        });
    }
}

void McpClient::shutdown()
{
    if (m_transport && m_transport->isOpen())
        m_transport->stop();
    m_initialized = false;
}

} // namespace LLMCore::Mcp
