// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

#include <LLMCore/LLMCore_global.h>

#include <LLMCore/BaseMessage.hpp>
#include <LLMCore/RequestMode.hpp>
#include <LLMCore/SSEBuffer.hpp>
#include <LLMCore/ToolSchemaFormat.hpp>

template<typename T>
class QFutureWatcher;
class QNetworkRequest;

namespace LLMCore {

class HttpClient;
class ToolsManager;

using RequestID = QString;

struct RequestCallbacks
{
    std::function<void(const RequestID &id, const QString &chunk)> onChunk;
    std::function<void(const RequestID &id, const QString &accumulated)> onAccumulated;
    std::function<void(const RequestID &id, const QString &thinking, const QString &signature)>
        onThinkingBlock;
    std::function<void(const RequestID &id, const QString &toolId, const QString &toolName)>
        onToolStarted;
    std::function<void(
        const RequestID &id, const QString &toolId, const QString &toolName, const QString &result)>
        onToolResult;
    std::function<void(const RequestID &id, const QString &fullText)> onCompleted;
    std::function<void(const RequestID &id, const QString &error)> onFailed;
};

struct DataBuffers
{
    SSEBuffer rawStreamBuffer;
    QString responseContent;

    void clear()
    {
        rawStreamBuffer.clear();
        responseContent.clear();
    }
};

struct ActiveRequest
{
    QFutureWatcher<QByteArray> *watcher = nullptr;
    DataBuffers buffers;
    RequestCallbacks callbacks;

    QUrl url;
    QJsonObject originalPayload;
    int continuationCount = 0;
    int emittedThinkingBlocksCount = 0;
    RequestMode mode = RequestMode::Streaming;
};

class LLMCORE_EXPORT BaseClient : public QObject
{
    Q_OBJECT
public:
    explicit BaseClient(QObject *parent = nullptr);
    explicit BaseClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    ~BaseClient() override;

    virtual RequestID sendMessage(
        const QJsonObject &payload,
        RequestCallbacks callbacks = {},
        RequestMode mode = RequestMode::Streaming)
        = 0;
    virtual RequestID ask(
        const QString &prompt,
        RequestCallbacks callbacks = {},
        RequestMode mode = RequestMode::Streaming)
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
    bool hasTools() const;

signals:
    void chunkReceived(const LLMCore::RequestID &id, const QString &chunk);
    void accumulatedReceived(const LLMCore::RequestID &id, const QString &accumulated);
    void requestCompleted(const LLMCore::RequestID &id, const QString &fullText);
    void requestFailed(const LLMCore::RequestID &id, const QString &error);
    void thinkingBlockReceived(
        const LLMCore::RequestID &id, const QString &thinking, const QString &signature);
    void toolStarted(const LLMCore::RequestID &id, const QString &toolId, const QString &toolName);
    void toolResultReady(
        const LLMCore::RequestID &id,
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
        const QHash<QString, QString> &toolResults)
        = 0;

    virtual void onStreamFinished(const RequestID &id, std::optional<QString> error);

    HttpClient *httpClient() const;
    RequestID createRequest(RequestCallbacks callbacks = {});
    void sendRequest(
        const RequestID &id,
        const QUrl &url,
        const QJsonObject &payload,
        RequestMode mode = RequestMode::Streaming);

    void addChunk(const RequestID &id, const QString &chunk);
    void notifyThinkingBlock(const RequestID &id, const QString &thinking, const QString &signature);
    void notifyToolStarted(const RequestID &id, const QString &toolId, const QString &toolName);
    void notifyToolResult(
        const RequestID &id, const QString &toolId, const QString &toolName, const QString &result);
    void completeRequest(const RequestID &id);
    void failRequest(const RequestID &id, const QString &error);

    void executeToolsFromMessage(const RequestID &id);
    void cleanupFullRequest(const RequestID &id);
    void notifyPendingThinkingBlocks(const RequestID &id);

    void storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload);
    bool checkContinuationLimit(const RequestID &id);

    bool hasRequest(const RequestID &id) const;
    SSEBuffer &requestSSEBuffer(const RequestID &id);
    QString responseContent(const RequestID &id) const;
    void setResponseContent(const RequestID &id, const QString &content);

    QString m_url;
    QString m_apiKey;
    QString m_model;

    static constexpr int kMaxToolContinuations = 10;

private:
    void handleToolContinuation(const RequestID &id, const QHash<QString, QString> &toolResults);
    void cleanupRequest(const RequestID &id);
    void startHttpRequest(
        const RequestID &id,
        const QNetworkRequest &request,
        const QJsonObject &payload,
        RequestMode mode);

    HttpClient *m_httpClient;
    ToolsManager *m_toolsManager = nullptr;
    QHash<RequestID, ActiveRequest> m_requests;
};

} // namespace LLMCore
