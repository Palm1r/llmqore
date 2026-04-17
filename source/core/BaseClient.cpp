// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseClient.hpp>

#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QUuid>

#include <LLMQore/HttpClient.hpp>
#include <LLMQore/HttpStream.hpp>
#include <LLMQore/HttpTransportError.hpp>
#include <LLMQore/Log.hpp>
#include <LLMQore/ToolsManager.hpp>

namespace LLMQore {

BaseClient::BaseClient(QObject *parent)
    : LLMQore::BaseClient({}, {}, {}, parent)
{}

BaseClient::BaseClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : QObject(parent)
    , m_url(url)
    , m_apiKey(apiKey)
    , m_model(model)
    , m_httpClient(new HttpClient(this))
{}

BaseClient::~BaseClient()
{
    for (auto it = m_requests.begin(); it != m_requests.end(); ++it) {
        if (it->stream) {
            it->stream->disconnect();
            it->stream->abort();
        }
    }
    m_requests.clear();
}

QString BaseClient::url() const
{
    return m_url;
}

void BaseClient::setUrl(const QString &url)
{
    m_url = url;
}

QString BaseClient::apiKey() const
{
    return m_apiKey;
}

void BaseClient::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey;
}

QString BaseClient::model() const
{
    return m_model;
}

void BaseClient::setModel(const QString &model)
{
    m_model = model;
}

HttpClient *BaseClient::httpClient() const
{
    return m_httpClient;
}

ToolsManager *BaseClient::tools()
{
    if (!m_toolsManager) {
        m_toolsManager = new ToolsManager(toolSchemaFormat(), this);

        // Direct signal-to-signal forward: Qt re-emits with argument copy
        // and respects the owning thread's event loop. No slot needed.
        connect(
            m_toolsManager, &ToolsManager::toolExecutionStarted,
            this, &BaseClient::toolStarted);
        connect(
            m_toolsManager, &ToolsManager::toolExecutionResult,
            this, &BaseClient::toolResultReady);
        connect(
            m_toolsManager,
            &ToolsManager::toolExecutionComplete,
            this,
            &BaseClient::handleToolContinuation);
    }
    return m_toolsManager;
}

bool BaseClient::hasTools() const noexcept
{
    return m_toolsManager != nullptr;
}

int BaseClient::maxToolContinuations() const noexcept
{
    return m_maxToolContinuations;
}

void BaseClient::setMaxToolContinuations(int limit) noexcept
{
    m_maxToolContinuations = limit > 0 ? limit : 1;
}

RequestID BaseClient::createRequest()
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::createRequest called from non-owning thread");
    RequestID id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_requests[id] = ActiveRequest{};
    return id;
}

void BaseClient::sendRequest(
    const RequestID &id, const QUrl &url, const QJsonObject &payload, RequestMode mode)
{
    storeRequestContext(id, url, payload);
    auto it = m_requests.find(id);
    if (it != m_requests.end())
        it->mode = mode;
    startHttpRequest(id, prepareNetworkRequest(url), payload, mode);
}

QString BaseClient::parseHttpError(const HttpResponse &response) const
{
    constexpr int kSnippetCap = 512;
    if (response.body.isEmpty())
        return QString("HTTP %1").arg(response.statusCode);
    const QString snippet = QString::fromUtf8(response.body.left(kSnippetCap));
    return QString("HTTP %1: %2").arg(response.statusCode).arg(snippet);
}

void BaseClient::startHttpRequest(
    const RequestID &id,
    const QNetworkRequest &request,
    const QJsonObject &payload,
    RequestMode mode)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    if (mode == RequestMode::Buffered) {
        m_httpClient->send(request, QByteArrayView("POST"), body)
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
            .onFailed(this, [this, id](const HttpTransportError &e) {
                if (hasRequest(id))
                    onStreamFinished(id, e.message());
            })
            .onFailed(this, [this, id](const std::exception &e) {
                if (hasRequest(id))
                    onStreamFinished(id, QString::fromUtf8(e.what()));
            });
        return;
    }

    HttpStream *stream = m_httpClient->openStream(request, QByteArrayView("POST"), body);
    it->stream = stream;
    it->errorMode = false;
    it->errorBody.clear();

    QPointer<HttpStream> guardedStream(stream);

    connect(stream, &HttpStream::headersReceived, this, [this, id, guardedStream]() {
        if (!guardedStream)
            return;
        auto it = m_requests.find(id);
        if (it == m_requests.end() || it->stream != guardedStream)
            return;
        const int status = guardedStream->statusCode();
        if (status < 200 || status >= 300)
            it->errorMode = true;
    });

    connect(stream, &HttpStream::chunkReceived, this, [this, id, guardedStream](const QByteArray &chunk) {
        if (!guardedStream)
            return;
        auto it = m_requests.find(id);
        if (it == m_requests.end() || it->stream != guardedStream)
            return;
        if (it->errorMode) {
            it->errorBody.append(chunk);
            return;
        }
        processData(id, chunk);
    });

    connect(stream, &HttpStream::finished, this, [this, id, guardedStream]() {
        if (!guardedStream) {
            return;
        }
        auto it = m_requests.find(id);
        if (it == m_requests.end() || it->stream != guardedStream) {
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

    connect(stream, &HttpStream::errorOccurred, this,
            [this, id, guardedStream](const HttpTransportError &e) {
        if (!guardedStream)
            return;
        auto it = m_requests.find(id);
        if (it == m_requests.end() || it->stream != guardedStream) {
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

// cleanupFullRequest() intentionally does NOT touch m_requests.
// completeRequest()/failRequest() then extract data from m_requests and remove the entry.
// This ordering must be preserved.
void BaseClient::onStreamFinished(const RequestID &id, std::optional<QString> error)
{
    if (error) {
        cleanupFullRequest(id);
        failRequest(id, *error);
        return;
    }

    auto *msg = messageForRequest(id);
    if (msg && msg->state() == MessageState::RequiresToolExecution)
        return;

    // Capture finalisation metadata BEFORE cleanupFullRequest destroys the
    // message via cleanupDerivedData(). This is what completeRequest() then
    // reads to populate CompletionInfo::stopReason for onFinalized consumers.
    if (msg) {
        auto it = m_requests.find(id);
        if (it != m_requests.end())
            it->stopReason = msg->stopReason();
    }

    cleanupFullRequest(id);
    completeRequest(id);
}

void BaseClient::addChunk(const RequestID &id, const QString &chunk)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::addChunk called from non-owning thread");
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    it->buffers.responseContent += chunk;

    emit chunkReceived(id, chunk);
    emit accumulatedReceived(id, it->buffers.responseContent);
}

void BaseClient::completeRequest(const RequestID &id)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::completeRequest called from non-owning thread");
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    QString fullText = it->buffers.responseContent;
    QString stopReason = it->stopReason;
    cleanupRequest(id);

    // requestFinalized carries the rich metadata (model, stopReason) and
    // is emitted BEFORE requestCompleted so that consumers resolving a
    // QPromise / future on finalization see the resolution first, before
    // any simple "fullText-only" handler runs.
    CompletionInfo info;
    info.fullText = fullText;
    info.model = m_model;
    info.stopReason = stopReason;
    emit requestFinalized(id, info);
    emit requestCompleted(id, fullText);
}

void BaseClient::failRequest(const RequestID &id, const QString &error)
{
    Q_ASSERT_X(thread() == QThread::currentThread(), Q_FUNC_INFO,
               "BaseClient::failRequest called from non-owning thread");
    if (!m_requests.contains(id))
        return;

    cleanupRequest(id);
    emit requestFailed(id, error);
}

void BaseClient::cancelRequest(const RequestID &requestId)
{
    auto it = m_requests.find(requestId);
    if (it == m_requests.end())
        return;

    if (it->stream) {
        it->stream->disconnect();
        it->stream->abort();
        it->stream->deleteLater();
        it->stream = nullptr;
    }

    cleanupFullRequest(requestId);
    failRequest(requestId, QStringLiteral("Request cancelled"));
}

void BaseClient::executeToolsFromMessage(const RequestID &id)
{
    auto *msg = messageForRequest(id);
    if (!msg)
        return;

    if (msg->state() != MessageState::RequiresToolExecution)
        return;

    auto toolUseContent = msg->getCurrentToolUseContent();
    if (toolUseContent.isEmpty())
        return;

    for (auto *toolContent : toolUseContent) {
        tools()->executeToolCall(id, toolContent->id(), toolContent->name(), toolContent->input());
    }
}

void BaseClient::handleToolContinuation(
    const RequestID &id, const QHash<QString, ToolResult> &toolResults)
{
    auto *message = messageForRequest(id);
    auto it = m_requests.find(id);
    if (!message || it == m_requests.end() || it->url.isEmpty()) {
        qCWarning(llmQoreLog).noquote()
            << QString("Missing data for continuation request %1").arg(id);
        cleanupFullRequest(id);
        failRequest(id, "Missing data for tool continuation");
        return;
    }

    if (!checkContinuationLimit(id)) {
        qCWarning(llmQoreLog).noquote()
            << QString("Tool continuation limit reached for request %1").arg(id);
        cleanupFullRequest(id);
        failRequest(id, "Tool continuation limit reached");
        return;
    }

    QJsonObject payload = buildContinuationPayload(it->originalPayload, message, toolResults);

    sendRequest(id, it->url, payload, it->mode);
}

void BaseClient::cleanupFullRequest(const RequestID &id)
{
    cleanupDerivedData(id);

    auto it = m_requests.find(id);
    if (it != m_requests.end()) {
        it->url.clear();
        it->originalPayload = {};
        it->continuationCount = 0;
        it->emittedThinkingBlocksCount = 0;
        it->mode = RequestMode::Streaming;
    }

    if (m_toolsManager)
        m_toolsManager->cleanupRequest(id);
}

void BaseClient::notifyPendingThinkingBlocks(const RequestID &id)
{
    auto *message = messageForRequest(id);
    if (!message)
        return;

    auto thinkingBlocks = message->getCurrentThinkingContent();
    if (thinkingBlocks.isEmpty())
        return;

    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    int alreadyEmitted = it->emittedThinkingBlocksCount;
    int totalBlocks = thinkingBlocks.size();

    for (int i = alreadyEmitted; i < totalBlocks; ++i) {
        auto *thinkingContent = thinkingBlocks[i];
        if (!thinkingContent->thinking().trimmed().isEmpty()) {
            emit thinkingBlockReceived(
                id, thinkingContent->thinking(), thinkingContent->signature());
        }
    }

    it->emittedThinkingBlocksCount = totalBlocks;
}

void BaseClient::storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    it->url = url;
    it->originalPayload = payload;
    it->buffers.lineBuffer.clear();
    it->buffers.sseParser.clear();
}

bool BaseClient::checkContinuationLimit(const RequestID &id)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return false;
    ++it->continuationCount;
    return it->continuationCount <= m_maxToolContinuations;
}

bool BaseClient::hasRequest(const RequestID &id) const noexcept
{
    return m_requests.contains(id);
}

LineBuffer &BaseClient::requestLineBuffer(const RequestID &id)
{
    auto it = m_requests.find(id);
    Q_ASSERT(it != m_requests.end());
    return it->buffers.lineBuffer;
}

SSEParser &BaseClient::requestSSEParser(const RequestID &id)
{
    auto it = m_requests.find(id);
    Q_ASSERT(it != m_requests.end());
    return it->buffers.sseParser;
}

QString BaseClient::responseContent(const RequestID &id) const
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return {};
    return it->buffers.responseContent;
}

void BaseClient::setResponseContent(const RequestID &id, const QString &content)
{
    auto it = m_requests.find(id);
    if (it != m_requests.end())
        it->buffers.responseContent = content;
}

void BaseClient::cleanupRequest(const RequestID &id)
{
    auto it = m_requests.find(id);
    if (it == m_requests.end())
        return;

    if (it->stream) {
        it->stream->disconnect();
        it->stream->deleteLater();
        it->stream = nullptr;
    }

    m_requests.erase(it);
}

} // namespace LLMQore
