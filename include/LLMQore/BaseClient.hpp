// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <QFuture>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaType>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <LLMQore/HttpResponse.hpp>
#include <LLMQore/LLMQore_global.h>

#include <LLMQore/BaseMessage.hpp>
#include <LLMQore/Conversation.hpp>
#include <LLMQore/RequestMode.hpp>
#include <LLMQore/RpcLineFramer.hpp>
#include <LLMQore/SSEParser.hpp>
#include <LLMQore/ToolResult.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/UsageSchema.hpp>

namespace LLMQore {

class HttpStreamHandle;
class HttpTransport;
class ToolsManager;

using RequestID = QString;

struct LLMQORE_EXPORT AuthScheme
{
    enum class Placement { Header, QueryParam, None };

    Placement placement = Placement::Header;
    QString name;
    QString valuePrefix;
};

struct LLMQORE_EXPORT TokenUsage
{
    int promptTokens = 0;
    int completionTokens = 0;
    int cachedPromptTokens = 0;
    int reasoningTokens = 0;

    bool isValid() const noexcept { return promptTokens > 0 || completionTokens > 0; }
    int totalTokens() const noexcept { return promptTokens + completionTokens; }
};

struct LLMQORE_EXPORT ModelInfo
{
    QString id;
    QString displayName;

    std::optional<int> maxOutputTokens = std::nullopt;
    std::optional<int> maxInputTokens = std::nullopt;

    std::optional<bool> supportsImageInput = std::nullopt;
    std::optional<bool> supportsThinking = std::nullopt;
    std::optional<bool> supportsToolCalls = std::nullopt;
    std::optional<bool> supportsStructuredOutputs = std::nullopt;
};

struct LLMQORE_EXPORT CompletionInfo
{
    QString fullText;
    QString model;
    QString stopReason;
    std::optional<TokenUsage> usage;

    QJsonObject requestPayload;
    Conversation conversation;
};

class LLMQORE_EXPORT BaseClient : public QObject
{
    Q_OBJECT
public:
    explicit BaseClient(QObject *parent = nullptr);
    explicit BaseClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);
    explicit BaseClient(
        const QString &url,
        const QString &apiKey,
        const QString &model,
        HttpTransport *transport,
        QObject *parent = nullptr);
    ~BaseClient() override;

    virtual RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming)
        = 0;
    virtual RequestID ask(
        const QString &prompt, RequestMode mode = RequestMode::Streaming)
        = 0;
    RequestID ask(
        const Conversation &conversation,
        const QJsonObject &extra = {},
        RequestMode mode = RequestMode::Streaming);
    virtual QJsonObject buildConversationPayload(const Conversation &conversation) const = 0;

    QFuture<CompletionInfo> askOnce(
        const QString &prompt, RequestMode mode = RequestMode::Streaming);
    QFuture<CompletionInfo> askOnce(
        const Conversation &conversation,
        const QJsonObject &extra = {},
        RequestMode mode = RequestMode::Streaming);
    virtual QFuture<QList<ModelInfo>> listModels(const QString &endpoint = {}) = 0;

    [[nodiscard]] std::optional<ModelInfo> cachedModel(const QString &id) const;
    [[nodiscard]] const QList<ModelInfo> &cachedModels() const noexcept;
    void clearModelCache();
    void cancelRequest(const RequestID &requestId);

    QString url() const;
    void setUrl(const QString &url);

    QString apiKey() const;
    void setApiKey(const QString &apiKey);

    QString model() const;
    void setModel(const QString &model);

    AuthScheme authScheme() const;
    void setAuthScheme(const AuthScheme &scheme);

    QHash<QString, QString> headers() const;
    void setHeader(const QString &name, const QString &value);
    void setHeaders(const QHash<QString, QString> &headers);

    ToolsManager *tools();
    bool hasTools() const noexcept;

    static constexpr int kDefaultMaxToolRounds = 10;

    int maxToolContinuations() const;
    void setMaxToolContinuations(int limit);

    [[nodiscard]] int toolRounds(const RequestID &id) const;

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
    [[nodiscard]] QJsonObject attachToolDefinitions(QJsonObject payload) const;

    virtual const ToolDialect &toolDialect() const = 0;

    virtual const UsageSchema &usageSchema() const = 0;

    virtual void processData(const RequestID &id, const QByteArray &data);
    void processBufferedResponse(const RequestID &id, const QByteArray &data);
    virtual void processBufferedBody(const RequestID &id, const QJsonObject &body) = 0;
    virtual QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults)
        = 0;

    virtual void cleanupDerivedData(const RequestID &id);

    [[nodiscard]] const QLoggingCategory &logCategory() const;
    void setLogCategory(const QLoggingCategory &category);

    struct ErrorAnnotation
    {
        QString label;
        QString field;
    };
    [[nodiscard]] virtual QList<ErrorAnnotation> errorAnnotations() const;

    [[nodiscard]] QString errorMessageFrom(const QJsonObject &body) const;
    [[nodiscard]] QString httpErrorSnippet(const HttpResponse &response) const;
    [[nodiscard]] virtual QString parseHttpError(const HttpResponse &response) const;

    using ModelInfoEnricher = std::function<void(const QJsonObject &, ModelInfo &)>;

    [[nodiscard]] QFuture<QList<ModelInfo>> fetchModelList(
        const QUrl &url,
        const QString &arrayKey = QStringLiteral("data"),
        const QString &idKey = QStringLiteral("id"),
        const std::function<QString(QString)> &idMapper = {},
        const ModelInfoEnricher &enrich = {});

    [[nodiscard]] QUrl endpointUrl(const QString &endpoint, const QString &defaultPath) const;

    [[nodiscard]] BaseMessage *messageForRequest(const RequestID &id) const;
    void setMessageForRequest(const RequestID &id, BaseMessage *message);

    template<typename T>
    T *ensureMessage(const RequestID &id)
    {
        if (auto *existing = qobject_cast<T *>(messageForRequest(id))) {
            if (existing->state() == MessageState::RequiresToolExecution)
                existing->startNewContinuation();
            return existing;
        }
        auto *created = new T(this);
        setMessageForRequest(id, created);
        return created;
    }

    template<typename T>
    [[nodiscard]] T *messageAs(const RequestID &id) const
    {
        return qobject_cast<T *>(messageForRequest(id));
    }

    [[nodiscard]] static QJsonObject appendChatMessagesContinuation(
        const QJsonObject &originalPayload,
        const QJsonObject &assistantMessage,
        const QJsonArray &toolMessages);

    template<typename T>
    [[nodiscard]] static QJsonObject appendChatContinuation(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults)
    {
        auto *typed = qobject_cast<T *>(message);
        if (!typed)
            return originalPayload;

        return appendChatMessagesContinuation(
            originalPayload,
            typed->toProviderFormat(),
            typed->createToolResultMessages(toolResults));
    }

    virtual void onStreamFinished(const RequestID &id, std::optional<QString> error);

    virtual std::optional<QString> takePendingStreamError(const RequestID &id);

    virtual void onStreamDrained(const RequestID &id);

    QFuture<CompletionInfo> trackOneShot(const std::function<RequestID()> &dispatch);
    void resolveOneShot(const RequestID &id, const CompletionInfo &info);
    void rejectOneShot(const RequestID &id, const QString &error);

    virtual void flushStreamBuffers(const RequestID &id);

    void dispatchSseEvents(const RequestID &id, const QList<SSEEvent> &events);

    virtual void processSseEvent(
        const RequestID &id, const SSEEvent &event, const QJsonObject &json);

    [[nodiscard]] HttpTransport *transport() const;
    [[nodiscard]] QNetworkRequest prepareNetworkRequest(const QUrl &url) const;
    [[nodiscard]] RequestID createRequest();
    void sendRequest(
        const RequestID &id,
        const QUrl &url,
        const QJsonObject &payload,
        RequestMode mode = RequestMode::Streaming);

    void addChunk(const RequestID &id, const QString &chunk);
    void completeRequest(const RequestID &id);
    void failRequest(const RequestID &id, const QString &error);

    void captureStopReason(const RequestID &id);

    void applyUsage(const RequestID &id, const QJsonObject &root);
    void applyUsage(const RequestID &id, const QJsonObject &root, const UsageSchema &schema);

    void executeToolsFromMessage(const RequestID &id);
    void cleanupFullRequest(const RequestID &id);
    void notifyPendingThinkingBlocks(const RequestID &id);

    void storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload);

    bool hasRequest(const RequestID &id) const noexcept;
    Rpc::LineFramer &requestLineFramer(const RequestID &id);
    SSEParser &requestSSEParser(const RequestID &id);
    QString responseContent(const RequestID &id) const;
    void setResponseContent(const RequestID &id, const QString &content);

    QString m_url;
    QString m_apiKey;
    QString m_model;

private:
    void handleToolsCompleted(
        const RequestID &id, const QHash<QString, ToolResult> &toolResults);
    void setUsage(const RequestID &id, const TokenUsage &usage);
    [[nodiscard]] std::optional<TokenUsage> currentUsage(const RequestID &id) const;
    void finalizeTurn(const RequestID &id);

    void continueRequest(const RequestID &id, const QJsonObject &payload);
    void abortRequest(const RequestID &id, const QString &error);
    [[nodiscard]] QJsonObject buildReplayContinuation(
        const RequestID &id, const QHash<QString, ToolResult> &toolResults);

    void cleanupRequest(const RequestID &id);
    void startHttpRequest(
        const RequestID &id,
        const QNetworkRequest &request,
        const QJsonObject &payload,
        RequestMode mode);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore

Q_DECLARE_METATYPE(LLMQore::TokenUsage)
Q_DECLARE_METATYPE(LLMQore::CompletionInfo)
