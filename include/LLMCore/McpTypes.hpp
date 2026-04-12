// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <LLMCore/LLMCore_global.h>

namespace LLMCore::Mcp {

// TODO: revisit on spec bump.
inline constexpr const char *kProtocolVersion2025_11_25 = "2025-11-25";
inline constexpr const char *kProtocolVersion2025_06_18 = "2025-06-18";
inline constexpr const char *kProtocolVersion2025_03_26 = "2025-03-26";
inline constexpr const char *kProtocolVersion2024_11_05 = "2024-11-05";
inline constexpr const char *kSupportedProtocolVersion  = kProtocolVersion2025_11_25;

inline constexpr const char *kKnownProtocolVersions[] = {
    kProtocolVersion2025_11_25,
    kProtocolVersion2025_06_18,
    kProtocolVersion2025_03_26,
    kProtocolVersion2024_11_05,
};

namespace ErrorCode {
inline constexpr int ParseError           = -32700;
inline constexpr int InvalidRequest       = -32600;
inline constexpr int MethodNotFound       = -32601;
inline constexpr int InvalidParams        = -32602;
inline constexpr int InternalError        = -32603;
inline constexpr int ServerNotInitialized = -32002;
inline constexpr int RequestCancelled     = -32800;
inline constexpr int ContentTooLarge      = -32801;
} // namespace ErrorCode

struct LLMCORE_EXPORT IconInfo
{
    QString src;
    QString mimeType;
    QString sizes;

    QJsonObject toJson() const;
    static IconInfo fromJson(const QJsonObject &obj);
};

LLMCORE_EXPORT QJsonArray iconsToJson(const QList<IconInfo> &icons);
LLMCORE_EXPORT QList<IconInfo> iconsFromJson(const QJsonArray &arr);

struct LLMCORE_EXPORT Implementation
{
    QString name;
    QString version;
    QString description;
    QString title;
    QList<IconInfo> icons;

    QJsonObject toJson() const;
    static Implementation fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ToolsCapability
{
    bool listChanged = false;

    QJsonObject toJson() const;
    static ToolsCapability fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ResourcesCapability
{
    bool subscribe = false;
    bool listChanged = false;

    QJsonObject toJson() const;
    static ResourcesCapability fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT PromptsCapability
{
    bool listChanged = false;

    QJsonObject toJson() const;
    static PromptsCapability fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT LoggingCapability
{
    bool present = true;

    QJsonObject toJson() const;
    static LoggingCapability fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT CompletionsCapability
{
    bool present = true;

    QJsonObject toJson() const;
    static CompletionsCapability fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ServerCapabilities
{
    std::optional<ToolsCapability> tools;
    std::optional<ResourcesCapability> resources;
    std::optional<PromptsCapability> prompts;
    std::optional<LoggingCapability> logging;
    std::optional<CompletionsCapability> completions;
    QJsonObject extras;

    QJsonObject toJson() const;
    static ServerCapabilities fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT RootsCapability
{
    bool listChanged = false;

    QJsonObject toJson() const;
    static RootsCapability fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT SamplingCapability
{
    bool present = true;

    QJsonObject toJson() const;
    static SamplingCapability fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ElicitationCapability
{
    bool present = true;

    QJsonObject toJson() const;
    static ElicitationCapability fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ClientCapabilities
{
    std::optional<RootsCapability> roots;
    std::optional<SamplingCapability> sampling;
    std::optional<ElicitationCapability> elicitation;
    QJsonObject extras;

    QJsonObject toJson() const;
    static ClientCapabilities fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT InitializeResult
{
    QString protocolVersion;
    ServerCapabilities capabilities;
    Implementation serverInfo;
    QString instructions;

    QJsonObject toJson() const;
    static InitializeResult fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ToolInfo
{
    QString name;
    QString title;
    QString description;
    QJsonObject inputSchema;
    QJsonObject outputSchema;
    QJsonObject annotations;
    QList<IconInfo> icons;
    QJsonObject meta;

    QJsonObject toJson() const;
    static ToolInfo fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ResourceInfo
{
    QString uri;
    QString name;
    QString title;
    QString description;
    QString mimeType;
    QList<IconInfo> icons;
    QJsonObject meta;

    QJsonObject toJson() const;
    static ResourceInfo fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ResourceTemplate
{
    QString uriTemplate;
    QString name;
    QString title;
    QString description;
    QString mimeType;
    QList<IconInfo> icons;
    QJsonObject meta;

    QJsonObject toJson() const;
    static ResourceTemplate fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ResourceContents
{
    QString uri;
    QString mimeType;
    QString text;
    QByteArray blob;

    QJsonObject toJson() const;
    static ResourceContents fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT PromptArgument
{
    QString name;
    QString description;
    bool required = false;

    QJsonObject toJson() const;
    static PromptArgument fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT PromptInfo
{
    QString name;
    QString title;
    QString description;
    QList<PromptArgument> arguments;
    QList<IconInfo> icons;
    QJsonObject meta;

    QJsonObject toJson() const;
    static PromptInfo fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT PromptMessage
{
    QString role;
    QJsonObject content;

    QJsonObject toJson() const;
    static PromptMessage fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT PromptGetResult
{
    QString description;
    QList<PromptMessage> messages;

    QJsonObject toJson() const;
    static PromptGetResult fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT CompletionReference
{
    QString type;
    QString name;
    QString uri;

    QJsonObject toJson() const;
    static CompletionReference fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT CompletionArgument
{
    QString name;
    QString value;

    QJsonObject toJson() const;
    static CompletionArgument fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT CompletionResult
{
    QStringList values;
    std::optional<int> total;
    bool hasMore = false;

    QJsonObject toJson() const;
    static CompletionResult fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT Root
{
    QString uri;
    QString name;

    QJsonObject toJson() const;
    static Root fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT SamplingMessage
{
    QString role;
    QJsonObject content;

    QJsonObject toJson() const;
    static SamplingMessage fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ModelHint
{
    QString name;

    QJsonObject toJson() const;
    static ModelHint fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ModelPreferences
{
    QList<ModelHint> hints;
    std::optional<double> costPriority;
    std::optional<double> speedPriority;
    std::optional<double> intelligencePriority;

    QJsonObject toJson() const;
    static ModelPreferences fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT CreateMessageParams
{
    QList<SamplingMessage> messages;
    std::optional<ModelPreferences> modelPreferences;
    QString systemPrompt;
    QString includeContext;
    std::optional<double> temperature;
    int maxTokens = 0;
    QStringList stopSequences;
    QJsonObject metadata;

    QJsonObject toJson() const;
    static CreateMessageParams fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT CreateMessageResult
{
    QString role;
    QJsonObject content;
    QString model;
    QString stopReason;

    QJsonObject toJson() const;
    static CreateMessageResult fromJson(const QJsonObject &obj);
};

struct LLMCORE_EXPORT ElicitRequestParams
{
    QString message;
    QJsonObject requestedSchema;
    QString mode;
    QString url;

    QJsonObject toJson() const;
    static ElicitRequestParams fromJson(const QJsonObject &obj);
};

namespace ElicitAction {
inline constexpr const char *Accept  = "accept";
inline constexpr const char *Decline = "decline";
inline constexpr const char *Cancel  = "cancel";
} // namespace ElicitAction

struct LLMCORE_EXPORT ElicitResult
{
    QString action;
    QJsonObject content;

    QJsonObject toJson() const;
    static ElicitResult fromJson(const QJsonObject &obj);
};

namespace LogLevel {
inline constexpr const char *Debug     = "debug";
inline constexpr const char *Info      = "info";
inline constexpr const char *Notice    = "notice";
inline constexpr const char *Warning   = "warning";
inline constexpr const char *Error     = "error";
inline constexpr const char *Critical  = "critical";
inline constexpr const char *Alert     = "alert";
inline constexpr const char *Emergency = "emergency";
} // namespace LogLevel

} // namespace LLMCore::Mcp
