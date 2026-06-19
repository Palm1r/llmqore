# ACP wire types + serialization idiom

ACP types live in `include/LLMQore/AcpTypes.hpp` + `source/acp/AcpTypes.cpp`, namespace
`LLMQore::Acp`. They follow the **exact** idiom already used by
[`McpTypes`](../../../include/LLMQore/McpTypes.hpp): a plain struct per wire object,
`LLMQORE_EXPORT`, value members with sane defaults, `std::optional<T>` for
genuinely-optional sub-objects, and a serialization pair:

```cpp
struct LLMQORE_EXPORT InitializeResult
{
    int protocolVersion = 0;               // NOTE: integer on the wire (ACP), not a date string
    AgentCapabilities agentCapabilities;
    std::optional<Implementation> agentInfo;
    QList<AuthMethod> authMethods;

    QJsonObject toJson() const;
    static InitializeResult fromJson(const QJsonObject &obj);
};
```

This matches `Mcp::InitializeResult` field-for-idiom; the one deliberate divergence is
`protocolVersion` (see the gotcha below).

## Type groups

| Group | Structs |
|---|---|
| Init | `InitializeParams`, `InitializeResult`, `ClientCapabilities`, `AgentCapabilities`, `PromptCapabilities`, `McpCapabilities`, `AuthMethod` |
| Session | `NewSessionParams`, `NewSessionResult`, `LoadSessionParams`, `SessionMode`, `SessionModeState`, `SessionConfigOption` |
| Prompt | `PromptParams`, `PromptResult` (`stopReason`) |
| Content | `ContentBlock` (tagged union), `Annotations`, `EmbeddedResource` |
| Updates | `SessionUpdate` (tagged union), `ToolCall`, `ToolCallUpdate`, `ToolCallContent`, `ToolCallLocation`, `Plan`, `PlanEntry`, `AvailableCommand` |
| Permission | `RequestPermissionParams`, `PermissionOption`, `RequestPermissionResult`, `PermissionOutcome` |
| FS | `ReadTextFileParams/Result`, `WriteTextFileParams/Result` |
| Terminal | `CreateTerminalParams/Result`, `TerminalOutputParams/Result`, `WaitForExit*`, `Kill*`, `Release*`, `ExitStatus`, `EnvVariable` |

## Tagged unions

ACP discriminates `ContentBlock` and `SessionUpdate` by a string tag. Represent each as
a struct with a `type` (or `sessionUpdate`) field plus the union of possible payload
members; `fromJson` switches on the tag, `toJson` emits only the active variant's
fields. This is the same approach `Mcp::PromptMessage`/content handling already takes
(content kept as `QJsonObject` where the shape is open-ended).

```cpp
struct LLMQORE_EXPORT ContentBlock
{
    QString type;        // "text" | "image" | "audio" | "resource_link" | "resource"
    QString text;        // type == "text"
    QString data;        // image/audio: base64
    QString mimeType;    // image/audio/resource_link
    QString uri;         // resource_link / image(optional)
    std::optional<EmbeddedResource> resource;  // type == "resource"
    std::optional<Annotations> annotations;
    QJsonObject meta;

    QJsonObject toJson() const;
    static ContentBlock fromJson(const QJsonObject &obj);
};
```

`SessionUpdate` is discriminated by `sessionUpdate`:
`user_message_chunk`, `agent_message_chunk`, `agent_thought_chunk`, `tool_call`,
`tool_call_update`, `plan`, `available_commands_update`, `current_mode_update`.

## ContentBlock ↔ existing `ContentBlocks`

The repo already has [`ContentBlocks`](../../../include/LLMQore/ContentBlocks.hpp) for
the provider stack. The variants overlap; map between them at the boundary rather than
forcing one type to serve both wire formats:

| ACP `ContentBlock` | Overlaps existing | Notes |
|---|---|---|
| `text` | yes | direct |
| `image` (base64 `data` + `mimeType`) | yes | direct |
| `audio` | partial | gated by `promptCapabilities.audio` |
| `resource_link` (uri + metadata) | — | ACP-specific, keep as-is |
| `resource` (embedded text/blob) | partial | gated by `promptCapabilities.embeddedContext` |

Keep `Acp::ContentBlock` as the wire type; provide free helper functions to convert
to/from the provider-stack content when a host wants to share rendering code.

## Error codes

Reuse the JSON-RPC error constants already defined in `McpTypes`
(`ErrorCode::{ParseError, InvalidRequest, MethodNotFound, InvalidParams,
InternalError, RequestCancelled}`) — they are standard JSON-RPC 2.0 codes, not
MCP-specific, so they belong in the shared `Rpc` layer alongside `JsonRpcSession`.

## The `protocolVersion` gotcha

The single idiom divergence from `McpTypes`: ACP's `protocolVersion` is an **integer**
(`uint16`), whereas MCP uses **date strings** (`"2025-11-25"`). Do not copy
`Mcp::kSupportedProtocolVersion` (a `const char*`) for ACP — define an `int`
constant and serialize it as a JSON number. Mixing these up yields a silent
`initialize` rejection that is annoying to debug.

## Invariant

- **Every wire struct round-trips.** `T::fromJson(t.toJson()) == t` for all types, with
  unknown fields preserved in a `meta`/`extras` `QJsonObject` (same tolerance rule as
  `Mcp::ServerCapabilities::extras`). Covered by `tst_AcpTypes`.
