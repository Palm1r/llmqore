// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <memory>

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/LLMQore_global.h>

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/LineBuffer.hpp>
#include <LLMQore/RequestMode.hpp>
#include <LLMQore/SSEParser.hpp>
#include <LLMQore/ToolResult.hpp>
#include <LLMQore/ToolSchemaFormat.hpp>

namespace LLMQore {

class HttpClient;
class HttpStream;
class ToolsManager;

using RequestID = QString;

struct LLMQORE_EXPORT CompletionInfo
{
    QString fullText;
    QString model;
    QString stopReason;
};

struct DataBuffers
{
    LineBuffer lineBuffer;
    SSEParser sseParser;
    QString responseContent;

    void clear()
    {
        lineBuffer.clear();
        sseParser.clear();
        responseContent.clear();
    }
};

struct ActiveRequest
{
    HttpStream *stream = nullptr;

    bool errorMode = false;
    QByteArray errorBody = {};

    DataBuffers buffers = {};

    QUrl url = {};
    QJsonObject originalPayload = {};
    int continuationCount = 0;
    int emittedThinkingBlocksCount = 0;
    RequestMode mode = RequestMode::Streaming;
    QString stopReason = {};
};

/*!
 * Abstract base for LLM provider clients.
 *
 * Threading: a BaseClient lives on the thread of the QObject parent passed
 * to its constructor. All public methods must be called from that thread,
 * and all signals are emitted on that thread. Debug builds enforce this
 * with Q_ASSERT_X.
 *
 * Observing progress: subscribe to the signals declared below
 * (chunkReceived, requestCompleted, requestFinalized, requestFailed,
 * toolStarted, toolResultReady, thinkingBlockReceived). Qt's default
 * AutoConnection queues delivery and copies arguments safely when the
 * receiver lives on a different thread — no manual marshalling required.
 *
 * RequestID lifetime: the ID returned by sendMessage / ask is the only
 * handle for cancelRequest(); losing it forfeits the ability to cancel.
 */
class LLMQORE_EXPORT BaseClient : public QObject
{
    Q_OBJECT
public:
    explicit BaseClient(QObject *parent = nullptr);
    explicit BaseClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    ~BaseClient() override;

    // `endpoint` lets callers override the path the request goes to; pass
    // an empty string to use the provider's default (e.g. /v1/messages
    // for Claude, /chat/completions for OpenAI/Mistral). Mistral uses it
    // to target /fim/completions in addition to /chat/completions; most
    // providers expose only one endpoint and ignore non-empty values
    // unless documented otherwise.
    virtual RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming)
        = 0;
    virtual RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming)
        = 0;
    virtual QFuture<QList<QString>> listModels() = 0;
    void cancelRequest(const RequestID &requestId);

    QString url() const;
    void setUrl(const QString &url);

    QString apiKey() const;
    void setApiKey(const QString &apiKey);

    QString model() const;
    void setModel(const QString &model);

    ToolsManager *tools();
    bool hasTools() const noexcept;

    int maxToolContinuations() const noexcept;
    void setMaxToolContinuations(int limit) noexcept;

signals:
    void chunkReceived(const LLMQore::RequestID &id, const QString &chunk);
    void accumulatedReceived(const LLMQore::RequestID &id, const QString &accumulated);
    void requestCompleted(const LLMQore::RequestID &id, const QString &fullText);
    // Emitted alongside requestCompleted, with richer metadata. Consumers
    // that only need fullText can use requestCompleted; those that also
    // want model/stopReason should prefer requestFinalized. Emitted before
    // requestCompleted so code resolving a QPromise on requestFinalized
    // sees the resolution before any signal-chain on requestCompleted.
    void requestFinalized(const LLMQore::RequestID &id, const LLMQore::CompletionInfo &info);
    void requestFailed(const LLMQore::RequestID &id, const QString &error);
    void thinkingBlockReceived(
        const LLMQore::RequestID &id, const QString &thinking, const QString &signature);
    void toolStarted(const LLMQore::RequestID &id, const QString &toolId, const QString &toolName);
    void toolResultReady(
        const LLMQore::RequestID &id,
        const QString &toolId,
        const QString &toolName,
        const QString &result);

protected:
    virtual ToolSchemaFormat toolSchemaFormat() const = 0;

    virtual void processData(const RequestID &id, const QByteArray &data) = 0;
    virtual void processBufferedResponse(const RequestID &id, const QByteArray &data) = 0;
    virtual QNetworkRequest prepareNetworkRequest(const QUrl &url) const = 0;
    virtual BaseMessage *messageForRequest(const RequestID &id) const = 0;
    virtual void cleanupDerivedData(const RequestID &id) = 0;
    virtual QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults)
        = 0;

    [[nodiscard]] virtual QString parseHttpError(const HttpResponse &response) const;

    virtual void onStreamFinished(const RequestID &id, std::optional<QString> error);

    HttpClient *httpClient() const;
    [[nodiscard]] RequestID createRequest();
    void sendRequest(
        const RequestID &id,
        const QUrl &url,
        const QJsonObject &payload,
        RequestMode mode = RequestMode::Streaming);

    void addChunk(const RequestID &id, const QString &chunk);
    void completeRequest(const RequestID &id);
    void failRequest(const RequestID &id, const QString &error);

    void executeToolsFromMessage(const RequestID &id);
    void cleanupFullRequest(const RequestID &id);
    void notifyPendingThinkingBlocks(const RequestID &id);

    void storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload);
    bool checkContinuationLimit(const RequestID &id);

    bool hasRequest(const RequestID &id) const noexcept;
    LineBuffer &requestLineBuffer(const RequestID &id);
    SSEParser &requestSSEParser(const RequestID &id);
    QString responseContent(const RequestID &id) const;
    void setResponseContent(const RequestID &id, const QString &content);

    QString m_url;
    QString m_apiKey;
    QString m_model;

    static constexpr int kMaxToolContinuations = 10;

private:
    void handleToolContinuation(const RequestID &id, const QHash<QString, ToolResult> &toolResults);
    void cleanupRequest(const RequestID &id);
    void startHttpRequest(
        const RequestID &id,
        const QNetworkRequest &request,
        const QJsonObject &payload,
        RequestMode mode);

    HttpClient *m_httpClient;
    ToolsManager *m_toolsManager = nullptr;
    QHash<RequestID, ActiveRequest> m_requests;
    int m_maxToolContinuations = kMaxToolContinuations;
};

} // namespace LLMQore
