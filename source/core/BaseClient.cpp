// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseClient.hpp>

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QUrlQuery>
#include <QPromise>
#include <QUuid>

#include <stdexcept>
#include <utility>
#include <variant>

#include "Usage.hpp"
#include <LLMQore/FutureUtils.hpp>
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpTransport.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/RpcLineFramer.hpp>
#include <LLMQore/SSEParser.hpp>
#include <LLMQore/ToolsManager.hpp>

#include "core/ThreadAffinity.hpp"

namespace LLMQore {

namespace {

// One request frames its bytes one way. Carrying both framers meant every request
// paid for the one it never used, and both parser headers leaked into the public
// BaseClient.hpp for the sake of a member nobody but Ollama reads.
struct DataBuffers
{
    std::variant<SSEParser, Rpc::LineFramer> framer;
    QString responseContent;

    void reset(StreamFraming framing)
    {
        if (framing == StreamFraming::JsonLines)
            framer.emplace<Rpc::LineFramer>();
        else
            framer.emplace<SSEParser>();
        responseContent.clear();
    }

    void clear()
    {
        std::visit([](auto &active) { active.clear(); }, framer);
        responseContent.clear();
    }
};

struct ActiveRequest
{
    QPointer<HttpStreamHandle> stream;

    bool errorMode = false;
    QByteArray errorBody = {};

    DataBuffers buffers = {};

    QUrl url = {};
    QJsonObject originalPayload = {};
    QJsonObject finalPayload = {};
    RequestMode mode = RequestMode::Streaming;
    QString stopReason = {};
    std::optional<TokenUsage> usage = {};
    std::optional<TokenUsage> turnUsage = {};
    int toolRounds = 0;
    Conversation conversation = {};
    bool tracksConversation = false;
    QList<TurnContent> finalBlocks = {};
    int roundTextOffset = 0;

    QPointer<BaseMessage> message;
};

} // namespace

struct BaseClient::Impl
{
    HttpTransport *transport = nullptr;
    QHash<RequestID, std::shared_ptr<QPromise<CompletionInfo>>> oneShots;
    std::shared_ptr<QPromise<CompletionInfo>> pendingOneShot;
    AuthScheme authScheme;
    QHash<QString, QString> headers;
    ToolsManager *toolsManager = nullptr;
    int maxToolRounds = BaseClient::kDefaultMaxToolRounds;
    ProviderProfile profile;
    QHash<RequestID, ActiveRequest> requests;
    QList<ModelInfo> modelCache;
    QHash<QString, int> modelIndex;
};

namespace {
constexpr qsizetype kMaxErrorBodyBytes = 64 * 1024;
} // namespace

BaseClient::BaseClient(QObject *parent)
    : LLMQore::BaseClient({}, {}, {}, parent)
{}

BaseClient::BaseClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : LLMQore::BaseClient(url, apiKey, model, nullptr, parent)
{}

BaseClient::BaseClient(
    const QString &url,
    const QString &apiKey,
    const QString &model,
    HttpTransport *transport,
    QObject *parent)
    : QObject(parent)
    , m_url(url)
    , m_apiKey(apiKey)
    , m_model(model)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->transport = transport ? transport : new HttpClient(this);
}

BaseClient::~BaseClient()
{
    for (auto it = m_impl->requests.begin(); it != m_impl->requests.end(); ++it) {
        if (!it->stream)
            continue;
        it->stream->disconnect();
        it->stream->abort();
        delete it->stream;
    }
    m_impl->requests.clear();

    const auto abandoned = std::exchange(m_impl->oneShots, {});
    for (const auto &promise : abandoned) {
        if (!promise)
            continue;
        promise->setException(std::make_exception_ptr(
            std::runtime_error("client destroyed before the request finished")));
        promise->finish();
    }
}

QString BaseClient::url() const
{
    LLMQORE_ASSERT_OWNING_THREAD();
    return m_url;
}

void BaseClient::setUrl(const QString &url)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    if (m_url == url)
        return;
    m_url = url;
    clearModelCache();
}

QString BaseClient::apiKey() const
{
    LLMQORE_ASSERT_OWNING_THREAD();
    return m_apiKey;
}

void BaseClient::setApiKey(const QString &apiKey)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    if (m_apiKey == apiKey)
        return;
    m_apiKey = apiKey;
    clearModelCache();
}

QString BaseClient::model() const
{
    LLMQORE_ASSERT_OWNING_THREAD();
    return m_model;
}

void BaseClient::setModel(const QString &model)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_model = model;
}

AuthScheme BaseClient::authScheme() const
{
    LLMQORE_ASSERT_OWNING_THREAD();
    return m_impl->authScheme;
}

void BaseClient::setAuthScheme(const AuthScheme &scheme)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_impl->authScheme = scheme;
}

QHash<QString, QString> BaseClient::headers() const
{
    LLMQORE_ASSERT_OWNING_THREAD();
    return m_impl->headers;
}

void BaseClient::setHeader(const QString &name, const QString &value)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_impl->headers.insert(name, value);
}

void BaseClient::setHeaders(const QHash<QString, QString> &headers)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_impl->headers = headers;
}

QNetworkRequest BaseClient::prepareNetworkRequest(const QUrl &url) const
{
    QNetworkRequest request(url);

    for (auto it = m_impl->headers.cbegin(); it != m_impl->headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    if (m_apiKey.isEmpty() || m_impl->authScheme.name.isEmpty())
        return request;

    switch (m_impl->authScheme.placement) {
    case AuthScheme::Placement::Header:
        request.setRawHeader(
            m_impl->authScheme.name.toUtf8(), (m_impl->authScheme.valuePrefix + m_apiKey).toUtf8());
        break;
    case AuthScheme::Placement::QueryParam: {
        QUrl requestUrl = request.url();
        QUrlQuery query(requestUrl.query());
        query.addQueryItem(m_impl->authScheme.name, m_impl->authScheme.valuePrefix + m_apiKey);
        requestUrl.setQuery(query);
        request.setUrl(requestUrl);
        break;
    }
    case AuthScheme::Placement::None:
        break;
    }

    return request;
}

HttpTransport *BaseClient::transport() const
{
    return m_impl->transport;
}

int BaseClient::transferTimeoutMs() const
{
    LLMQORE_ASSERT_OWNING_THREAD();
    return m_impl->transport->transferTimeoutMs();
}

void BaseClient::setTransferTimeout(int milliseconds)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_impl->transport->setTransferTimeout(milliseconds);
}

ToolsManager *BaseClient::tools()
{
    LLMQORE_ASSERT_OWNING_THREAD();
    if (!m_impl->toolsManager) {
        m_impl->toolsManager = new ToolsManager(toolDialect(), this);

        connect(
            m_impl->toolsManager, &ToolsManager::toolExecutionStarted,
            this, &BaseClient::toolStarted);
        connect(
            m_impl->toolsManager, &ToolsManager::toolExecutionResult,
            this, &BaseClient::toolResultReady);
        connect(
            m_impl->toolsManager,
            &ToolsManager::toolExecutionComplete,
            this,
            &BaseClient::handleToolsCompleted);
    }
    return m_impl->toolsManager;
}

bool BaseClient::hasTools() const noexcept
{
    return m_impl->toolsManager != nullptr;
}

int BaseClient::maxToolContinuations() const
{
    LLMQORE_ASSERT_OWNING_THREAD();
    return m_impl->maxToolRounds;
}

void BaseClient::setMaxToolContinuations(int limit)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_impl->maxToolRounds = limit > 0 ? limit : 1;
}

int BaseClient::toolRounds(const RequestID &id) const
{
    auto it = m_impl->requests.constFind(id);
    return it == m_impl->requests.constEnd() ? 0 : it->toolRounds;
}

void BaseClient::handleToolsCompleted(
    const RequestID &id, const QHash<QString, ToolResult> &toolResults)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    if (++it->toolRounds > m_impl->maxToolRounds) {
        qCWarning(logCategory()).noquote()
            << QString("Tool continuation limit reached for request %1").arg(id);
        abortRequest(id, QStringLiteral("Tool continuation limit reached"));
        return;
    }

    const QJsonObject payload = buildReplayContinuation(id, toolResults);
    if (payload.isEmpty()) {
        qCWarning(logCategory()).noquote()
            << QString("Missing data for continuation request %1").arg(id);
        abortRequest(id, QStringLiteral("Missing data for tool continuation"));
        return;
    }

    continueRequest(id, payload);
}

RequestID BaseClient::createRequest()
{
    RequestID id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    auto oneShot = std::exchange(m_impl->pendingOneShot, {});

    auto registerRequest = [this, id, oneShot = std::move(oneShot)]() mutable {
        m_impl->requests[id] = ActiveRequest{};
        m_impl->requests[id].buffers.reset(streamFraming());
        if (oneShot)
            m_impl->oneShots.insert(id, std::move(oneShot));
    };
    if (thread() == QThread::currentThread())
        registerRequest();
    else
        QMetaObject::invokeMethod(this, std::move(registerRequest), Qt::QueuedConnection);
    return id;
}

void BaseClient::sendRequest(
    const RequestID &id, const QUrl &url, const QJsonObject &payload, RequestMode mode)
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(
            this,
            [this, id, url, payload, mode]() { sendRequest(id, url, payload, mode); },
            Qt::QueuedConnection);
        return;
    }
    storeRequestContext(id, url, payload);
    auto it = m_impl->requests.find(id);
    if (it != m_impl->requests.end())
        it->mode = mode;
    startHttpRequest(id, prepareNetworkRequest(url), payload, mode);
}

const ProviderProfile &BaseClient::profile() const
{
    return m_impl->profile;
}

void BaseClient::setProfile(const ProviderProfile &profile)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_impl->profile = profile;
    setAuthScheme(profile.auth);
    if (!profile.headers.isEmpty())
        setHeaders(profile.headers);
}

const QLoggingCategory &BaseClient::logCategory() const
{
    return m_impl->profile.log ? *m_impl->profile.log : llmQoreLog();
}

RequestID BaseClient::postJson(const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    LLMQORE_ASSERT_OWNING_THREAD();

    const RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? m_impl->profile.chatPath : endpoint;

    qCDebug(logCategory()).noquote() << QString("Sending request %1 to %2").arg(id, resolved);

    sendRequest(id, QUrl(url() + resolved), payload, mode);
    return id;
}

RequestID BaseClient::ask(const QString &prompt, RequestMode mode)
{
    Conversation conversation;
    conversation.addUser(prompt);
    return ask(conversation, {}, mode);
}

void BaseClient::cleanupDerivedData(const RequestID &)
{}

BaseMessage *BaseClient::messageForRequest(const RequestID &id) const
{
    auto it = m_impl->requests.constFind(id);
    if (it == m_impl->requests.constEnd())
        return nullptr;
    return it->message.data();
}

void BaseClient::setMessageForRequest(const RequestID &id, BaseMessage *message)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end()) {
        if (message)
            message->deleteLater();
        return;
    }

    if (it->message == message)
        return;

    if (it->message)
        it->message->deleteLater();
    it->message = message;
}

QUrl BaseClient::endpointUrl(const QString &endpoint, const QString &defaultPath) const
{
    return QUrl(m_url + (endpoint.isEmpty() ? defaultPath : endpoint));
}

QFuture<QList<ModelInfo>> BaseClient::fetchModelList(
    const QUrl &url,
    const QString &arrayKey,
    const QString &idKey,
    const std::function<QString(QString)> &idMapper,
    const ModelInfoEnricher &enrich)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    const QNetworkRequest request = prepareNetworkRequest(url);
    const QLoggingCategory *cat = &logCategory();

    return LLMQore::compat(transport()->send(request, QByteArrayView("GET")))
        .then(this, [this, cat, arrayKey, idKey, idMapper, enrich](const HttpResponse &response) {
            const QLoggingCategory &log = *cat;

            QList<ModelInfo> models;
            if (!response.isSuccess()) {
                qCDebug(log).noquote()
                    << QString("Error fetching models: HTTP %1").arg(response.statusCode);
                return models;
            }

            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                qCDebug(log).noquote()
                    << QString("Error fetching models: malformed response (%1)")
                           .arg(parseError.errorString());
                return models;
            }

            const QJsonObject json = doc.object();
            const QJsonValue arrayValue = json.value(arrayKey);
            if (!arrayValue.isArray()) {
                qCDebug(log).noquote()
                    << QString("Error fetching models: response has no '%1' array").arg(arrayKey);
                return models;
            }

            const QJsonArray entries = arrayValue.toArray();
            for (const QJsonValue &value : entries) {
                const QJsonObject entry = value.toObject();

                QString id = entry.value(idKey).toString();
                if (id.isEmpty())
                    continue;
                if (idMapper)
                    id = idMapper(std::move(id));
                if (id.isEmpty())
                    continue;

                ModelInfo info;
                info.id = id;
                if (enrich)
                    enrich(entry, info);
                models.append(info);
            }

            m_impl->modelCache = models;
            m_impl->modelIndex.clear();
            m_impl->modelIndex.reserve(models.size());
            for (int i = 0; i < models.size(); ++i)
                m_impl->modelIndex.insert(models.at(i).id, i);

            return models;
        })
        .onFailed(this, [cat](const std::exception &e) {
            const QLoggingCategory &log = *cat;
            qCDebug(log).noquote() << QString("Error fetching models: %1").arg(e.what());
            return QList<ModelInfo>{};
        });
}

std::optional<ModelInfo> BaseClient::cachedModel(const QString &id) const
{
    LLMQORE_ASSERT_OWNING_THREAD();
    const auto it = m_impl->modelIndex.constFind(id);
    if (it == m_impl->modelIndex.constEnd())
        return std::nullopt;
    return m_impl->modelCache.at(*it);
}

const QList<ModelInfo> &BaseClient::cachedModels() const noexcept
{
    return m_impl->modelCache;
}

void BaseClient::clearModelCache()
{
    LLMQORE_ASSERT_OWNING_THREAD();
    m_impl->modelCache.clear();
    m_impl->modelIndex.clear();
}

QList<BaseClient::ErrorAnnotation> BaseClient::errorAnnotations() const
{
    return {};
}

QString BaseClient::errorMessageFrom(const QJsonObject &body) const
{
    const QJsonValue error = body.value(QLatin1String("error"));
    if (error.isString())
        return error.toString();
    if (!error.isObject())
        return {};

    const QJsonObject object = error.toObject();
    QString out = object.value(QLatin1String("message")).toString();
    if (out.isEmpty())
        return {};

    const QList<ErrorAnnotation> annotations = errorAnnotations();
    for (const ErrorAnnotation &annotation : annotations) {
        const QJsonValue value = object.value(annotation.field);
        QString text;
        if (value.isString())
            text = value.toString();
        else if (value.isDouble() && value.toInt() != 0)
            text = QString::number(value.toInt());
        if (text.isEmpty())
            continue;

        out += annotation.label.isEmpty() ? QString(" (%1)").arg(text)
                                          : QString(" (%1: %2)").arg(annotation.label, text);
    }
    return out;
}

QString BaseClient::httpErrorSnippet(const HttpResponse &response) const
{
    constexpr int kSnippetCap = 512;
    if (response.body.isEmpty())
        return QString("HTTP %1").arg(response.statusCode);
    const QString snippet = QString::fromUtf8(response.body.left(kSnippetCap));
    return QString("HTTP %1: %2").arg(response.statusCode).arg(snippet);
}

QString BaseClient::parseHttpError(const HttpResponse &response) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(response.body);
    if (!doc.isObject())
        return httpErrorSnippet(response);

    const QString message = errorMessageFrom(doc.object());
    if (message.isEmpty())
        return httpErrorSnippet(response);

    return QString("HTTP %1: %2").arg(response.statusCode).arg(message);
}

void BaseClient::applyEffects(const RequestID &id, const MessageEffects &effects)
{
    if (!effects.fullText.isEmpty())
        setResponseContent(id, effects.fullText);
    else if (!effects.fallbackText.isEmpty() && responseContent(id).isEmpty())
        setResponseContent(id, effects.fallbackText);

    if (effects.thinkingCompleted)
        notifyPendingThinkingBlocks(id);

    if (!effects.chunk.isEmpty())
        addChunk(id, effects.chunk);

    if (!effects.usage.isEmpty())
        applyUsage(id, effects.usage);

    if (effects.toolsReady)
        executeToolsFromMessage(id);
}

void BaseClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        failRequest(id, QStringLiteral("Invalid JSON in buffered response"));
        return;
    }

    const QJsonObject body = doc.object();
    const QString error = errorMessageFrom(body);
    if (!error.isEmpty()) {
        failRequest(id, error);
        return;
    }

    processBufferedBody(id, body);
}

void BaseClient::startHttpRequest(
    const RequestID &id,
    const QNetworkRequest &request,
    const QJsonObject &payload,
    RequestMode mode)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    if (mode == RequestMode::Buffered) {
        (void)LLMQore::compat(m_impl->transport->send(request, QByteArrayView("POST"), body))
            .then(this, [this, id](const HttpResponse &response) {
                if (!hasRequest(id))
                    return;
                if (!response.isSuccess()) {
                    QString msg = parseHttpError(response);
                    if (msg.isEmpty())
                        msg = QString("HTTP %1").arg(response.statusCode);
                    onStreamFinished(id, msg);
                    return;
                }
                processBufferedResponse(id, response.body);
                onStreamFinished(id, std::nullopt);
            })
            .onFailed(this, [this, id](const auto &e) {
                if (!hasRequest(id))
                    return;
                if constexpr (std::is_same_v<std::decay_t<decltype(e)>, HttpTransportError>)
                    onStreamFinished(id, e.message());
                else
                    onStreamFinished(id, QString::fromUtf8(e.what()));
            });
        return;
    }

    HttpStreamHandle *stream = m_impl->transport->openStream(request, QByteArrayView("POST"), body);

    it = m_impl->requests.find(id);
    if (it == m_impl->requests.end()) {
        delete stream;
        return;
    }
    if (!stream) {
        onStreamFinished(id, QStringLiteral("Transport returned no stream"));
        return;
    }

    it->stream = stream;
    it->errorMode = false;
    it->errorBody.clear();

    QPointer<HttpStreamHandle> guardedStream(stream);

    connect(stream, &HttpStreamHandle::headersReceived, this, [this, id, guardedStream]() {
        if (!guardedStream)
            return;
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->stream != guardedStream)
            return;
        const int status = guardedStream->statusCode();
        if (status < 200 || status >= 300)
            it->errorMode = true;
    });

    connect(stream, &HttpStreamHandle::chunkReceived, this, [this, id, guardedStream](const QByteArray &chunk) {
        if (!guardedStream)
            return;
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->stream != guardedStream)
            return;
        if (it->errorMode) {
            const qsizetype room = kMaxErrorBodyBytes - it->errorBody.size();
            if (room > 0)
                it->errorBody.append(chunk.left(room));
            return;
        }
        processData(id, chunk);
    });

    connect(stream, &HttpStreamHandle::finished, this, [this, id, guardedStream]() {
        if (!guardedStream) {
            return;
        }
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->stream != guardedStream) {
            guardedStream->deleteLater();
            return;
        }

        std::optional<QString> error;
        if (it->errorMode) {
            HttpResponse r;
            r.statusCode = guardedStream->statusCode();
            r.rawHeaders = guardedStream->rawHeaders();
            r.body = it->errorBody;
            QString msg = parseHttpError(r);
            if (msg.isEmpty())
                msg = QString("HTTP %1").arg(r.statusCode);
            error = msg;
        }

        it->stream = nullptr;
        it->errorMode = false;
        it->errorBody.clear();
        guardedStream->disconnect();
        guardedStream->deleteLater();

        onStreamFinished(id, error);
    });

    connect(stream, &HttpStreamHandle::errorOccurred, this,
            [this, id, guardedStream](const HttpTransportError &e) {
        if (!guardedStream)
            return;
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->stream != guardedStream) {
            guardedStream->deleteLater();
            return;
        }
        it->stream = nullptr;
        it->errorMode = false;
        it->errorBody.clear();
        guardedStream->disconnect();
        guardedStream->deleteLater();
        onStreamFinished(id, e.message());
    });
}

void BaseClient::onStreamFinished(const RequestID &id, std::optional<QString> error)
{
    if (!error)
        error = takePendingStreamError(id);

    if (error) {
        cleanupFullRequest(id);
        failRequest(id, *error);
        return;
    }

    if (hasRequest(id))
        flushStreamBuffers(id);
    if (!hasRequest(id))
        return;

    onStreamDrained(id);
    if (!hasRequest(id))
        return;

    auto *msg = messageForRequest(id);
    if (msg && msg->state() == MessageState::RequiresToolExecution)
        return;

    captureStopReason(id);

    cleanupFullRequest(id);
    completeRequest(id);
}

void BaseClient::captureStopReason(const RequestID &id)
{
    auto *msg = messageForRequest(id);
    if (!msg)
        return;

    auto it = m_impl->requests.find(id);
    if (it != m_impl->requests.end())
        it->stopReason = msg->stopReason();
}

void BaseClient::addChunk(const RequestID &id, const QString &chunk)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    it->buffers.responseContent += chunk;
    const QString accumulated = it->buffers.responseContent;

    emit chunkReceived(id, chunk);
    if (!m_impl->requests.contains(id))
        return;
    emit accumulatedReceived(id, accumulated);
}

QJsonObject BaseClient::attachToolDefinitions(QJsonObject payload) const
{
    if (!m_impl->toolsManager)
        return payload;

    const QJsonArray definitions = m_impl->toolsManager->getToolsDefinitions();
    if (!definitions.isEmpty())
        payload.insert(QStringLiteral("tools"), definitions);

    return payload;
}

RequestID BaseClient::ask(
    const Conversation &conversation, const QJsonObject &extra, RequestMode mode)
{
    LLMQORE_ASSERT_OWNING_THREAD();

    QJsonObject payload = attachToolDefinitions(buildConversationPayload(conversation));
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        payload.insert(it.key(), it.value());

    const RequestID id = sendMessage(payload, {}, mode);

    auto request = m_impl->requests.find(id);
    if (request != m_impl->requests.end()) {
        request->conversation = conversation;
        request->tracksConversation = true;
    }

    return id;
}

QFuture<CompletionInfo> BaseClient::trackOneShot(const std::function<RequestID()> &dispatch)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    auto promise = std::make_shared<QPromise<CompletionInfo>>();
    promise->start();

    m_impl->pendingOneShot = promise;
    dispatch();
    const bool tracked = !m_impl->pendingOneShot;
    m_impl->pendingOneShot.reset();

    if (!tracked) {
        promise->setException(std::make_exception_ptr(
            std::runtime_error("request finished before it could be tracked")));
        promise->finish();
    }

    return promise->future();
}

void BaseClient::resolveOneShot(const RequestID &id, const CompletionInfo &info)
{
    if (auto promise = m_impl->oneShots.take(id)) {
        promise->addResult(info);
        promise->finish();
    }
}

void BaseClient::rejectOneShot(const RequestID &id, const QString &error)
{
    if (auto promise = m_impl->oneShots.take(id)) {
        promise->setException(
            std::make_exception_ptr(std::runtime_error(error.toStdString())));
        promise->finish();
    }
}

QFuture<CompletionInfo> BaseClient::askOnce(const QString &prompt, RequestMode mode)
{
    return trackOneShot([this, &prompt, mode]() { return ask(prompt, mode); });
}

QFuture<CompletionInfo> BaseClient::askOnce(
    const Conversation &conversation, const QJsonObject &extra, RequestMode mode)
{
    return trackOneShot(
        [this, &conversation, &extra, mode]() { return ask(conversation, extra, mode); });
}

void BaseClient::completeRequest(const RequestID &id)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    finalizeTurn(id);

    QString fullText = it->buffers.responseContent;
    QString stopReason = it->stopReason;
    std::optional<TokenUsage> usage = it->usage;
    QJsonObject requestPayload = it->finalPayload.isEmpty() ? it->originalPayload
                                                           : it->finalPayload;

    Conversation conversation;
    if (it->tracksConversation) {
        conversation = it->conversation;
        if (!it->finalBlocks.isEmpty()) {
            conversation.addAssistant(it->finalBlocks);
        } else {
            const int offset
                = std::clamp(it->roundTextOffset, 0, int(fullText.size()));
            const QString roundText = fullText.mid(offset);
            if (!roundText.isEmpty())
                conversation.addAssistant(roundText);
        }
    }

    cleanupRequest(id);

    CompletionInfo info;
    info.fullText = fullText;
    info.model = m_model;
    info.stopReason = stopReason;
    info.usage = usage;
    info.requestPayload = requestPayload;
    info.conversation = conversation;
    emit requestFinalized(id, info);
    emit requestCompleted(id, fullText);
    resolveOneShot(id, info);
}

void BaseClient::setUsage(const RequestID &id, const TokenUsage &usage)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;
    it->turnUsage = usage;
}

std::optional<TokenUsage> BaseClient::currentUsage(const RequestID &id) const
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return std::nullopt;
    return it->turnUsage;
}

void BaseClient::applyUsage(const RequestID &id, const QJsonObject &root)
{
    applyUsage(id, root, usageSchema());
}

void BaseClient::applyUsage(
    const RequestID &id, const QJsonObject &root, const UsageSchema &schema)
{
    const UsageDelta delta = parseUsage(root, schema);
    if (delta.isEmpty())
        return;

    setUsage(id, applyTo(delta, currentUsage(id).value_or(TokenUsage{})));
}

void BaseClient::finalizeTurn(const RequestID &id)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end() || !it->turnUsage)
        return;

    if (!it->usage) {
        it->usage = it->turnUsage;
    } else {
        it->usage->promptTokens += it->turnUsage->promptTokens;
        it->usage->completionTokens += it->turnUsage->completionTokens;
        it->usage->cachedPromptTokens += it->turnUsage->cachedPromptTokens;
        it->usage->reasoningTokens += it->turnUsage->reasoningTokens;
    }
    it->turnUsage.reset();
}

void BaseClient::failRequest(const RequestID &id, const QString &error)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    if (!m_impl->requests.contains(id))
        return;

    cleanupRequest(id);
    emit requestFailed(id, error);
    rejectOneShot(id, error);
}

void BaseClient::cancelRequest(const RequestID &requestId)
{
    abortRequest(requestId, QStringLiteral("Request cancelled"));
}

void BaseClient::abortRequest(const RequestID &id, const QString &error)
{
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(
            this, [this, id, error]() { abortRequest(id, error); }, Qt::QueuedConnection);
        return;
    }

    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    if (it->stream) {
        it->stream->disconnect();
        it->stream->abort();
        it->stream->deleteLater();
        it->stream = nullptr;
    }

    cleanupFullRequest(id);
    failRequest(id, error);
}

void BaseClient::executeToolsFromMessage(const RequestID &id)
{
    auto *msg = messageForRequest(id);
    if (!msg)
        return;

    if (msg->state() != MessageState::RequiresToolExecution)
        return;

    const auto toolUseContent = msg->currentToolUseContent();
    if (toolUseContent.isEmpty())
        return;

    QList<ToolRound::Call> calls;
    calls.reserve(toolUseContent.size());
    for (const auto &toolContent : toolUseContent)
        calls.append(ToolRound::Call{toolContent.id, toolContent.name, toolContent.input});

    tools()->beginRound(id, calls);
}

QJsonObject BaseClient::buildReplayContinuation(
    const RequestID &id, const QHash<QString, ToolResult> &toolResults)
{
    auto *message = messageForRequest(id);
    auto it = m_impl->requests.find(id);
    if (!message || it == m_impl->requests.end())
        return {};

    if (it->tracksConversation) {
        const QList<TurnContent> &blocks = message->currentBlocks();
        if (!blocks.isEmpty())
            it->conversation.addAssistant(blocks);

        QList<ToolResultContent> results;
        for (const ToolUseContent &use : message->currentToolUseContent()) {
            const auto result = toolResults.constFind(use.id);
            if (result == toolResults.constEnd())
                continue;
            results.append(makeToolResultContent(use.id, use.name, *result));
        }
        it->conversation.addToolResults(results);
    }

    return buildContinuationPayload(it->originalPayload, message, toolResults);
}

void BaseClient::continueRequest(const RequestID &id, const QJsonObject &payload)
{
    LLMQORE_ASSERT_OWNING_THREAD();
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end() || it->url.isEmpty()) {
        qCWarning(logCategory()).noquote()
            << QString("Missing transport context for continuation request %1").arg(id);
        cleanupFullRequest(id);
        failRequest(id, QStringLiteral("Missing data for tool continuation"));
        return;
    }

    finalizeTurn(id);
    it->roundTextOffset = int(it->buffers.responseContent.size());
    it->finalBlocks.clear();
    sendRequest(id, it->url, payload, it->mode);
}

void BaseClient::cleanupFullRequest(const RequestID &id)
{
    cleanupDerivedData(id);

    auto it = m_impl->requests.find(id);
    if (it != m_impl->requests.end()) {
        if (it->message) {
            it->finalBlocks = it->message->currentBlocks();
            it->message->deleteLater();
        }
        it->message = nullptr;
        it->url.clear();
        it->finalPayload = it->originalPayload;
        it->originalPayload = {};
        it->mode = RequestMode::Streaming;
    }

    if (m_impl->toolsManager)
        m_impl->toolsManager->cleanupRequest(id);
}

QJsonObject BaseClient::appendChatMessagesContinuation(
    const QJsonObject &originalPayload,
    const QJsonObject &assistantMessage,
    const QJsonArray &toolMessages)
{
    QJsonObject request = originalPayload;
    QJsonArray messages = request["messages"].toArray();

    messages.append(assistantMessage);
    for (const auto &toolMessage : toolMessages)
        messages.append(toolMessage);

    request["messages"] = messages;
    return request;
}

void BaseClient::notifyPendingThinkingBlocks(const RequestID &id)
{
    auto *message = messageForRequest(id);
    if (!message || !hasRequest(id))
        return;

    const QList<PendingThinkingNotification> pending
        = message->takePendingThinkingNotifications();

    for (const PendingThinkingNotification &notification : pending) {
        emit thinkingBlockReceived(id, notification.thinking, notification.signature);

        if (!hasRequest(id))
            return;
    }
}

void BaseClient::flushStreamBuffers(const RequestID &id)
{
    dispatchSseEvents(id, requestSSEParser(id).flush());
}

std::optional<QString> BaseClient::takePendingStreamError(const RequestID &)
{
    return std::nullopt;
}

void BaseClient::onStreamDrained(const RequestID &)
{}

void BaseClient::processData(const RequestID &id, const QByteArray &data)
{
    if (!hasRequest(id))
        return;

    dispatchSseEvents(id, requestSSEParser(id).append(data));
}

void BaseClient::dispatchSseEvents(const RequestID &id, const QList<SSEEvent> &events)
{
    for (int i = 0; i < events.size(); ++i) {
        const SSEEvent &ev = events.at(i);
        if (ev.data.isEmpty() || ev.data == "[DONE]")
            continue;

        const QJsonObject json = QJsonDocument::fromJson(ev.data).object();
        if (json.isEmpty())
            continue;

        processSseEvent(id, ev, json);

        if (!hasRequest(id)) {
            const int dropped = events.size() - i - 1;
            if (dropped > 0) {
                qCDebug(logCategory()).noquote()
                    << QString("Dropped %1 event(s) after the request ended: %2")
                           .arg(dropped)
                           .arg(id);
            }
            return;
        }
    }
}

void BaseClient::processSseEvent(const RequestID &, const SSEEvent &, const QJsonObject &)
{}

void BaseClient::storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    it->url = url;
    it->originalPayload = payload;
    it->buffers.clear();
}

bool BaseClient::hasRequest(const RequestID &id) const noexcept
{
    return m_impl->requests.contains(id);
}

StreamFraming BaseClient::streamFraming() const
{
    return StreamFraming::ServerSentEvents;
}

Rpc::LineFramer &BaseClient::requestLineFramer(const RequestID &id)
{
    auto it = m_impl->requests.find(id);
    Q_ASSERT(it != m_impl->requests.end());
    auto *framer = std::get_if<Rpc::LineFramer>(&it->buffers.framer);
    Q_ASSERT_X(framer != nullptr, Q_FUNC_INFO, "requestLineFramer needs JsonLines framing");
    return *framer;
}

SSEParser &BaseClient::requestSSEParser(const RequestID &id)
{
    auto it = m_impl->requests.find(id);
    Q_ASSERT(it != m_impl->requests.end());
    auto *parser = std::get_if<SSEParser>(&it->buffers.framer);
    Q_ASSERT_X(parser != nullptr, Q_FUNC_INFO, "requestSSEParser needs ServerSentEvents framing");
    return *parser;
}

QString BaseClient::responseContent(const RequestID &id) const
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return {};
    return it->buffers.responseContent;
}

void BaseClient::setResponseContent(const RequestID &id, const QString &content)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end() || it->buffers.responseContent == content)
        return;

    it->buffers.responseContent = content;
    emit accumulatedReceived(id, content);
}

void BaseClient::cleanupRequest(const RequestID &id)
{
    auto it = m_impl->requests.find(id);
    if (it == m_impl->requests.end())
        return;

    if (it->stream) {
        it->stream->disconnect();
        it->stream->abort();
        it->stream->deleteLater();
        it->stream = nullptr;
    }

    if (it->message)
        it->message->deleteLater();

    m_impl->requests.erase(it);
}

} // namespace LLMQore
