// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include <QByteArrayList>
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
#include <LLMQore/SSEEvent.hpp>
#include <LLMQore/ToolDialect.hpp>
#include <LLMQore/ToolResult.hpp>
#include <LLMQore/UsageSchema.hpp>

namespace LLMQore {

class HttpStreamHandle;
class HttpTransport;
class ToolsManager;

enum class StreamFraming { ServerSentEvents, JsonLines };

using RequestID = QString;

struct LLMQORE_EXPORT AuthScheme
{
    enum class Placement { Header, QueryParam, None };

    Placement placement = Placement::Header;
    QString name;
    QString valuePrefix;
};

struct LLMQORE_EXPORT ProviderProfile
{
    QString chatPath;
    QString modelsPath;
    const QLoggingCategory *log = nullptr;
    AuthScheme auth;
    QHash<QString, QString> headers{
        {QStringLiteral("Content-Type"), QStringLiteral("application/json")}};
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
    RequestID ask(const QString &prompt, RequestMode mode = RequestMode::Streaming);
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

    [[nodiscard]] const ProviderProfile &profile() const;
    void setProfile(const ProviderProfile &profile);

    static constexpr int kDefaultMaxToolRounds = 10;

    int maxToolContinuations() const;
    void setMaxToolContinuations(int limit);

    [[nodiscard]] int toolRounds(const RequestID &id) const;

    int transferTimeoutMs() const;
    void setTransferTimeout(int milliseconds);

    struct ErrorAnnotation
    {
        QString label;
        QString field;
    };

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
    virtual const ToolDialect &toolDialect() const = 0;

    virtual const UsageSchema &usageSchema() const = 0;

    [[nodiscard]] virtual StreamFraming streamFraming() const;

    virtual void processData(const RequestID &id, const QByteArray &data);
    virtual void processSseEvent(
        const RequestID &id, const SSEEvent &event, const QJsonObject &json);
    virtual void processJsonLine(const RequestID &id, const QJsonObject &json);
    virtual void processBufferedBody(const RequestID &id, const QJsonObject &body) = 0;
    virtual QJsonObject buildContinuationPayload(
        const QJsonObject &originalPayload,
        BaseMessage *message,
        const QHash<QString, ToolResult> &toolResults)
        = 0;

    virtual std::optional<QString> takePendingStreamError(const RequestID &id);
    virtual void onStreamDrained(const RequestID &id);
    virtual void cleanupDerivedData(const RequestID &id);

    void applyEffects(const RequestID &id, const MessageEffects &effects);

    [[nodiscard]] const QLoggingCategory &logCategory() const;

    RequestID postJson(const QJsonObject &payload, const QString &endpoint, RequestMode mode);

    [[nodiscard]] virtual QList<ErrorAnnotation> errorAnnotations() const;

    [[nodiscard]] QString errorMessageFrom(const QJsonObject &body) const;
    [[nodiscard]] QString describeError(const QJsonObject &error) const;
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

    [[nodiscard]] HttpTransport *transport() const;
    [[nodiscard]] QNetworkRequest prepareNetworkRequest(const QUrl &url) const;

    void addChunk(const RequestID &id, const QString &chunk);
    void completeRequest(const RequestID &id);
    void failRequest(const RequestID &id, const QString &error);

    void applyUsage(const RequestID &id, const QJsonObject &root);
    void applyUsage(const RequestID &id, const QJsonObject &root, const UsageSchema &schema);

    void cleanupFullRequest(const RequestID &id);

    bool hasRequest(const RequestID &id) const noexcept;

private:
    QString m_url;
    QString m_apiKey;
    QString m_model;

    [[nodiscard]] QJsonObject attachToolDefinitions(QJsonObject payload) const;
    void setMessageForRequest(const RequestID &id, BaseMessage *message);

    [[nodiscard]] RequestID createRequest();
    void sendRequest(
        const RequestID &id,
        const QUrl &url,
        const QJsonObject &payload,
        RequestMode mode = RequestMode::Streaming);
    void storeRequestContext(const RequestID &id, const QUrl &url, const QJsonObject &payload);
    void startHttpRequest(
        const RequestID &id,
        const QNetworkRequest &request,
        const QJsonObject &payload,
        RequestMode mode);

    void processBufferedResponse(const RequestID &id, const QByteArray &data);
    void dispatchSseEvents(const RequestID &id, const QList<SSEEvent> &events);
    void dispatchJsonLines(const RequestID &id, const QByteArrayList &lines);
    void flushStreamBuffers(const RequestID &id);
    void onStreamFinished(const RequestID &id, std::optional<QString> error);

    void replaceRoundText(const RequestID &id, const QString &text);
    [[nodiscard]] QString roundText(const RequestID &id) const;

    void captureStopReason(const RequestID &id);
    void notifyPendingThinkingBlocks(const RequestID &id);
    void executeToolsFromMessage(const RequestID &id);

    void handleToolsCompleted(
        const RequestID &id, const QHash<QString, ToolResult> &toolResults);
    void setUsage(const RequestID &id, const TokenUsage &usage);
    [[nodiscard]] std::optional<TokenUsage> currentUsage(const RequestID &id) const;
    void finalizeTurn(const RequestID &id);

    void continueRequest(const RequestID &id, const QJsonObject &payload);
    void abortRequest(const RequestID &id, const QString &error);
    [[nodiscard]] QJsonObject buildReplayContinuation(
        const RequestID &id, const QHash<QString, ToolResult> &toolResults);

    QFuture<CompletionInfo> trackOneShot(const std::function<RequestID()> &dispatch);
    void resolveOneShot(const RequestID &id, const CompletionInfo &info);
    void rejectOneShot(const RequestID &id, const QString &error);

    void cleanupRequest(const RequestID &id);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace LLMQore

Q_DECLARE_METATYPE(LLMQore::TokenUsage)
Q_DECLARE_METATYPE(LLMQore::CompletionInfo)
