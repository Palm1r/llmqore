// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <LLMQore/LLMQore_global.h>

// Wire types for Zed's Agent Client Protocol (ACP), host/client perspective.
namespace LLMQore::Acp {

inline constexpr int kAcpProtocolVersion = 1;

namespace StopReason {
inline constexpr const char *EndTurn         = "end_turn";
inline constexpr const char *MaxTokens       = "max_tokens";
inline constexpr const char *MaxTurnRequests = "max_turn_requests";
inline constexpr const char *Refusal         = "refusal";
inline constexpr const char *Cancelled       = "cancelled";
} // namespace StopReason

namespace PermissionOptionKind {
inline constexpr const char *AllowOnce   = "allow_once";
inline constexpr const char *AllowAlways = "allow_always";
inline constexpr const char *RejectOnce  = "reject_once";
inline constexpr const char *RejectAlways = "reject_always";
} // namespace PermissionOptionKind

// JSON-RPC method names (client -> agent unless noted).
namespace Method {
inline constexpr const char *Initialize        = "initialize";
inline constexpr const char *Authenticate      = "authenticate";
inline constexpr const char *NewSession        = "session/new";
inline constexpr const char *LoadSession       = "session/load";
inline constexpr const char *Prompt            = "session/prompt";
inline constexpr const char *Cancel            = "session/cancel";        // notification
inline constexpr const char *SetMode           = "session/set_mode";
// agent -> client
inline constexpr const char *SessionUpdate     = "session/update";        // notification
inline constexpr const char *RequestPermission = "session/request_permission";
inline constexpr const char *FsReadTextFile    = "fs/read_text_file";
inline constexpr const char *FsWriteTextFile   = "fs/write_text_file";
inline constexpr const char *TerminalCreate    = "terminal/create";
inline constexpr const char *TerminalOutput    = "terminal/output";
inline constexpr const char *TerminalWaitForExit = "terminal/wait_for_exit";
inline constexpr const char *TerminalKill      = "terminal/kill";
inline constexpr const char *TerminalRelease   = "terminal/release";
} // namespace Method

// --- Shared small types ---

struct LLMQORE_EXPORT EnvVariable
{
    QString name;
    QString value;

    QJsonObject toJson() const;
    static EnvVariable fromJson(const QJsonObject &obj);
};

LLMQORE_EXPORT QJsonArray envToJson(const QList<EnvVariable> &env);
LLMQORE_EXPORT QList<EnvVariable> envFromJson(const QJsonArray &arr);

struct LLMQORE_EXPORT Implementation
{
    QString name;
    QString version;
    QString title;

    QJsonObject toJson() const;
    static Implementation fromJson(const QJsonObject &obj);
};

// --- Initialization ---

struct LLMQORE_EXPORT FileSystemCapability
{
    bool readTextFile = false;
    bool writeTextFile = false;

    QJsonObject toJson() const;
    static FileSystemCapability fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT ClientCapabilities
{
    FileSystemCapability fs;
    bool terminal = false;

    QJsonObject toJson() const;
    static ClientCapabilities fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT PromptCapabilities
{
    bool image = false;
    bool audio = false;
    bool embeddedContext = false;

    QJsonObject toJson() const;
    static PromptCapabilities fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT McpCapabilities
{
    bool http = false;
    bool sse = false;

    QJsonObject toJson() const;
    static McpCapabilities fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT AgentCapabilities
{
    bool loadSession = false;
    PromptCapabilities promptCapabilities;
    McpCapabilities mcpCapabilities;

    QJsonObject toJson() const;
    static AgentCapabilities fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT AuthMethod
{
    QString id;
    QString name;
    QString description;

    QJsonObject toJson() const;
    static AuthMethod fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT InitializeParams
{
    int protocolVersion = kAcpProtocolVersion;
    ClientCapabilities clientCapabilities;
    std::optional<Implementation> clientInfo;

    QJsonObject toJson() const;
    static InitializeParams fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT InitializeResult
{
    int protocolVersion = kAcpProtocolVersion;
    AgentCapabilities agentCapabilities;
    QList<AuthMethod> authMethods;
    std::optional<Implementation> agentInfo;

    QJsonObject toJson() const;
    static InitializeResult fromJson(const QJsonObject &obj);
};

// --- MCP servers passed to the agent in session/new (stdio form) ---

struct LLMQORE_EXPORT McpServer
{
    QString name;
    QString command;
    QStringList args;
    QList<EnvVariable> env;

    QJsonObject toJson() const;
    static McpServer fromJson(const QJsonObject &obj);
};

// --- Session modes ---

struct LLMQORE_EXPORT SessionMode
{
    QString id;
    QString name;
    QString description;

    QJsonObject toJson() const;
    static SessionMode fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT SessionModeState
{
    QString currentModeId;
    QList<SessionMode> availableModes;

    QJsonObject toJson() const;
    static SessionModeState fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT NewSessionParams
{
    QString cwd;
    QList<McpServer> mcpServers;
    QStringList additionalDirectories;

    QJsonObject toJson() const;
    static NewSessionParams fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT NewSessionResult
{
    QString sessionId;
    std::optional<SessionModeState> modes;

    QJsonObject toJson() const;
    static NewSessionResult fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT LoadSessionParams
{
    QString sessionId;
    QString cwd;
    QList<McpServer> mcpServers;
    QStringList additionalDirectories;

    QJsonObject toJson() const;
    static LoadSessionParams fromJson(const QJsonObject &obj);
};

// --- Content ---

struct LLMQORE_EXPORT EmbeddedResource
{
    QString uri;
    QString mimeType;
    QString text;     // text resource
    QString blob;     // base64 binary resource

    QJsonObject toJson() const;
    static EmbeddedResource fromJson(const QJsonObject &obj);
};

// Tagged union over `type`:
//   "text"          -> text
//   "image"|"audio" -> data + mimeType (+ uri for image)
//   "resource_link" -> uri + name (+ description, mimeType, size, title)
//   "resource"      -> resource (EmbeddedResource)
struct LLMQORE_EXPORT ContentBlock
{
    QString type;
    QString text;
    QString data;
    QString mimeType;
    QString uri;
    QString name;
    QString description;
    QString title;
    std::optional<int> size;
    std::optional<EmbeddedResource> resource;
    QJsonObject annotations;

    QJsonObject toJson() const;
    static ContentBlock fromJson(const QJsonObject &obj);

    static ContentBlock makeText(const QString &text);
};

LLMQORE_EXPORT QJsonArray contentBlocksToJson(const QList<ContentBlock> &blocks);
LLMQORE_EXPORT QList<ContentBlock> contentBlocksFromJson(const QJsonArray &arr);

// --- Prompt ---

struct LLMQORE_EXPORT PromptParams
{
    QString sessionId;
    QList<ContentBlock> prompt;

    QJsonObject toJson() const;
    static PromptParams fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT PromptResult
{
    QString stopReason = StopReason::EndTurn;

    QJsonObject toJson() const;
    static PromptResult fromJson(const QJsonObject &obj);
};

// --- Tool calls ---

struct LLMQORE_EXPORT ToolCallLocation
{
    QString path;
    std::optional<int> line;

    QJsonObject toJson() const;
    static ToolCallLocation fromJson(const QJsonObject &obj);
};

// Tagged union over `type`:
//   "content"  -> content (ContentBlock)
//   "diff"     -> path + oldText/newText
//   "terminal" -> terminalId
struct LLMQORE_EXPORT ToolCallContent
{
    QString type = QStringLiteral("content");
    std::optional<ContentBlock> content;
    QString path;
    QString oldText;
    QString newText;
    QString terminalId;

    QJsonObject toJson() const;
    static ToolCallContent fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT ToolCall
{
    QString toolCallId;
    QString title;
    QString kind;     // read | edit | delete | move | search | execute | think | fetch | other
    QString status;   // pending | in_progress | completed | failed
    QList<ToolCallContent> content;
    QList<ToolCallLocation> locations;
    QJsonObject rawInput;
    QJsonObject rawOutput;

    QJsonObject toJson() const;
    static ToolCall fromJson(const QJsonObject &obj);
};

// --- Plan ---

struct LLMQORE_EXPORT PlanEntry
{
    QString content;
    QString priority; // high | medium | low
    QString status;   // pending | in_progress | completed

    QJsonObject toJson() const;
    static PlanEntry fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT Plan
{
    QList<PlanEntry> entries;

    QJsonObject toJson() const;
    static Plan fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT AvailableCommand
{
    QString name;
    QString description;
    QString inputHint;

    QJsonObject toJson() const;
    static AvailableCommand fromJson(const QJsonObject &obj);
};

// --- Session update (agent -> client notification payload) ---

namespace SessionUpdateKind {
inline constexpr const char *UserMessageChunk   = "user_message_chunk";
inline constexpr const char *AgentMessageChunk  = "agent_message_chunk";
inline constexpr const char *AgentThoughtChunk  = "agent_thought_chunk";
inline constexpr const char *ToolCall           = "tool_call";
inline constexpr const char *ToolCallUpdate     = "tool_call_update";
inline constexpr const char *Plan               = "plan";
inline constexpr const char *AvailableCommandsUpdate = "available_commands_update";
inline constexpr const char *CurrentModeUpdate  = "current_mode_update";
inline constexpr const char *UsageUpdate        = "usage_update";
} // namespace SessionUpdateKind

// Tagged union over `sessionUpdate`. Only the fields relevant to the active
// kind are populated.
struct LLMQORE_EXPORT SessionUpdate
{
    QString sessionUpdate;
    std::optional<ContentBlock> content;        // *_message_chunk / thought
    std::optional<Acp::ToolCall> toolCall;      // tool_call / tool_call_update
    std::optional<Acp::Plan> plan;              // plan
    QList<AvailableCommand> availableCommands;  // available_commands_update
    QString currentModeId;                      // current_mode_update

    QJsonObject toJson() const;
    static SessionUpdate fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT SessionNotification
{
    QString sessionId;
    SessionUpdate update;

    QJsonObject toJson() const;
    static SessionNotification fromJson(const QJsonObject &obj);
};

// --- Permission ---

struct LLMQORE_EXPORT PermissionOption
{
    QString optionId;
    QString name;
    QString kind; // see PermissionOptionKind

    QJsonObject toJson() const;
    static PermissionOption fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT RequestPermissionParams
{
    QString sessionId;
    Acp::ToolCall toolCall;
    QList<PermissionOption> options;

    QJsonObject toJson() const;
    static RequestPermissionParams fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT RequestPermissionResult
{
    QString outcome;   // "selected" | "cancelled"
    QString optionId;  // set when outcome == "selected"

    QJsonObject toJson() const;
    static RequestPermissionResult fromJson(const QJsonObject &obj);

    static RequestPermissionResult selected(const QString &optionId);
    static RequestPermissionResult cancelled();
};

// --- File system (agent -> client) ---

struct LLMQORE_EXPORT ReadTextFileParams
{
    QString sessionId;
    QString path;
    std::optional<int> line;
    std::optional<int> limit;

    QJsonObject toJson() const;
    static ReadTextFileParams fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT ReadTextFileResult
{
    QString content;

    QJsonObject toJson() const;
    static ReadTextFileResult fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT WriteTextFileParams
{
    QString sessionId;
    QString path;
    QString content;

    QJsonObject toJson() const;
    static WriteTextFileParams fromJson(const QJsonObject &obj);
};

// --- Terminal (agent -> client) ---

struct LLMQORE_EXPORT CreateTerminalParams
{
    QString sessionId;
    QString command;
    QStringList args;
    QString cwd;
    QList<EnvVariable> env;
    std::optional<int> outputByteLimit;

    QJsonObject toJson() const;
    static CreateTerminalParams fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT CreateTerminalResult
{
    QString terminalId;

    QJsonObject toJson() const;
    static CreateTerminalResult fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT ExitStatus
{
    std::optional<int> exitCode;
    QString signal;

    QJsonObject toJson() const;
    static ExitStatus fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT TerminalOutputParams
{
    QString sessionId;
    QString terminalId;

    QJsonObject toJson() const;
    static TerminalOutputParams fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT TerminalOutputResult
{
    QString output;
    bool truncated = false;
    std::optional<ExitStatus> exitStatus;

    QJsonObject toJson() const;
    static TerminalOutputResult fromJson(const QJsonObject &obj);
};

// terminal/wait_for_exit, terminal/kill, terminal/release all take
// { sessionId, terminalId }.
struct LLMQORE_EXPORT TerminalRefParams
{
    QString sessionId;
    QString terminalId;

    QJsonObject toJson() const;
    static TerminalRefParams fromJson(const QJsonObject &obj);
};

struct LLMQORE_EXPORT WaitForTerminalExitResult
{
    std::optional<int> exitCode;
    QString signal;

    QJsonObject toJson() const;
    static WaitForTerminalExitResult fromJson(const QJsonObject &obj);
};

} // namespace LLMQore::Acp

Q_DECLARE_METATYPE(LLMQore::Acp::ContentBlock)
Q_DECLARE_METATYPE(LLMQore::Acp::ToolCall)
Q_DECLARE_METATYPE(LLMQore::Acp::Plan)
Q_DECLARE_METATYPE(LLMQore::Acp::AvailableCommand)
Q_DECLARE_METATYPE(LLMQore::Acp::PromptResult)
Q_DECLARE_METATYPE(LLMQore::Acp::InitializeResult)
Q_DECLARE_METATYPE(LLMQore::Acp::NewSessionResult)
Q_DECLARE_METATYPE(LLMQore::Acp::SessionUpdate)
