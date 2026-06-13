// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <memory>

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QMetaType>
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
class ToolLoopRunner;
class ToolsManager;

using RequestID = QString;

struct LLMQORE_EXPORT TokenUsage
{
    int promptTokens = 0;
    int completionTokens = 0;
    int cachedPromptTokens = 0;
    int reasoningTokens = 0;

    bool isValid() const noexcept { return promptTokens > 0 || completionTokens > 0; }
    int totalTokens() const noexcept { return promptTokens + completionTokens; }
};

struct LLMQORE_EXPORT CompletionInfo
{
    QString fullText;
    QString model;
    QString stopReason;
    std::optional<TokenUsage> usage;
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
    int emittedThinkingBlocksCount = 0;
    RequestMode mode = RequestMode::Streaming;
    QString stopReason = {};
    std::optional<TokenUsage> usage = {};
    std::optional<TokenUsage> turnUsage = {};
};

class LLMQORE_EXPORT BaseClient : public QObject
{
    Q_OBJECT
public:
    explicit BaseClient(QObject *parent = nullptr);
    explicit BaseClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    ~BaseClient() override;

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
    
    ToolLoopRunner *toolLoop();

    int maxToolContinuations() const;
    void setMaxToolContinuations(int limit);

    virtual void continueRequest(const RequestID &id, const QJsonObject &payload);
    void abortRequest(const RequestID &id, const QString &error);
    QJsonObject buildReplayContinuation(
        const RequestID &id, const QHash<QString, ToolResult> &toolResults);

    int transferTimeoutMs() const;
    void setTransferTimeout(int milliseconds);

signals:
    void chunkReceived(const LLMQore::RequestID &id, const QString &chunk);
    void accumulatedReceived(const LLMQore::RequestID &id, const QString &accumulated);
    void requestCompleted(const LLMQore::RequestID &id, const QString &fullText);
    void requestFinalized(const LLMQore::RequestID &id, const LLMQore::CompletionInfo &info);
    void requestFailed(const LLMQore::RequestID &id, const QString &error);
    void thinkingBlockReceived(
        const LLMQore::RequestID &id, const QString &thinking, const QString &signature);
    void toolStarted(
        const LLMQore::RequestID &id,
        const QString &toolId,
        const QString &toolName,
        const QJsonObject &arguments);
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

    void setUsage(const RequestID &id, const TokenUsage &usage);
    void accumulateUsage(const RequestID &id, const TokenUsage &delta);
    std::optional<TokenUsage> currentUsage(const RequestID &id) const;
    std::optional<TokenUsage> totalUsage(const RequestID &id) const;
    void finalizeTurn(const RequestID &id);

    void executeToolsFromMessage(const RequestID &id);
    void cleanupFullRequest(const RequestID &id);
    void notifyPendingThinkingBlocks(const RequestID &id);

    void storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload);

    bool hasRequest(const RequestID &id) const noexcept;
    LineBuffer &requestLineBuffer(const RequestID &id);
    SSEParser &requestSSEParser(const RequestID &id);
    QString responseContent(const RequestID &id) const;
    void setResponseContent(const RequestID &id, const QString &content);

    QString m_url;
    QString m_apiKey;
    QString m_model;

private:
    void cleanupRequest(const RequestID &id);
    void startHttpRequest(
        const RequestID &id,
        const QNetworkRequest &request,
        const QJsonObject &payload,
        RequestMode mode);

    HttpClient *m_httpClient;
    ToolsManager *m_toolsManager = nullptr;
    ToolLoopRunner *m_toolLoop = nullptr;
    QHash<RequestID, ActiveRequest> m_requests;
};

} // namespace LLMQore

Q_DECLARE_METATYPE(LLMQore::TokenUsage)
Q_DECLARE_METATYPE(LLMQore::CompletionInfo)
