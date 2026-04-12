# MCP Protocol Coverage

Status of Model Context Protocol support in this library, cross-referenced with the
authoritative [MCP specification 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25).

**Legend**

- ✅ **Implemented** — working end-to-end, covered by tests or verified by live probe
- 🟡 **Partial** — usable but with documented gaps
- ❌ **Not implemented** — out of scope for the current MVP
- 🚫 **Will not implement** — not applicable to a client-library (e.g. server-side OAuth issuer)

Declared protocol version: `2025-11-25` (`include/LLMCore/McpTypes.hpp:30`).
Accepted during negotiation: `2025-11-25`, `2025-06-18`, `2025-03-26`, `2024-11-05`
(`include/LLMCore/McpTypes.hpp:35-40`).

---

## Summary by section

| Section                   | Coverage     |
|---------------------------|--------------|
| Base protocol & framing   | ✅ Full       |
| Lifecycle & negotiation   | ✅ Full       |
| Transport: stdio          | ✅ Full (client + server) |
| Transport: Streamable HTTP (2025-03-26+) | 🟡 Client + server (no long-lived GET push) |
| Transport: HTTP+SSE (2024-11-05 legacy)  | 🟡 Client only |
| Authorization             | ❌ Not implemented |
| Server → Tools            | ✅ Full (incl. rich content blocks, title, icons, outputSchema, _meta) |
| Server → Resources        | ✅ Full (incl. templates, title, icons, _meta) |
| Server → Prompts          | ✅ Full (list, get, list_changed) |
| Server → Logging method   | ✅ Full (setLevel + notifications/message) |
| Server → Completion       | ✅ Full (completion/complete for prompts + resource templates) |
| Client → Roots            | ✅ Full (list + list_changed, via BaseRootsProvider) |
| Client → Sampling         | ✅ Full (setSamplingClient wires any BaseClient; tool-calling-in-sampling works via existing ToolsManager loop) |
| Client → Elicitation      | 🟡 Plumbing only (BaseElicitationProvider + server-side createElicitation; host provides the UI) |
| Utility: Ping             | ✅ Both directions (client + server) |
| Utility: Cancellation     | ✅ Client-side cancel + server-side handler abort |
| Utility: Progress         | ✅ Progress tokens, `notifications/progress`, per-call callbacks |
| Utility: Tasks (experimental 2025-11-25) | ❌ Not implemented |
| Utility: `_meta` field    | ✅ Round-tripped on ToolInfo / ResourceInfo / ResourceTemplate / PromptInfo |
| Utility: `icons` (2025-11-25) | ✅ Round-tripped on Implementation / ToolInfo / ResourceInfo / ResourceTemplate / PromptInfo |
| JSON Schema 2020-12       | ✅ Compatible |

---

## 1. Base protocol

### Message format (JSON-RPC 2.0)

| Requirement | Status | Notes |
|---|---|---|
| Request with `id` (string or integer) | ✅ | `McpSession` allocates string ids from `QAtomicInteger` (`source/mcp/McpSession.cpp`). Strings, not integers, to avoid edge cases. |
| `id` MUST NOT be `null` in requests | ✅ | Enforced by the type of our counter. |
| `id` MUST be unique per session | ✅ | Monotonic `fetchAndAddRelaxed`. |
| Response echoes request `id` | ✅ | `McpSession::sendResponse`. |
| Error envelope `{ code, message, data? }` | ✅ | `McpSession::sendError`. |
| Notifications (no `id`) | ✅ | `McpSession::sendNotification`. |
| Standard error codes (-32700, -32600, -32601, -32602, -32603) | ✅ | Declared in `include/LLMCore/McpTypes.hpp`. Unknown methods auto-return `-32601`. |
| MCP-extension error codes (-32002, -32800, -32801) | ✅ | `-32800 RequestCancelled` actively returned by `dispatchRequest` when the handler's future throws `McpCancelledError`. |
| Server throws `McpRemoteError` → serialised to error envelope | ✅ | `source/mcp/McpSession.cpp`. |

### Lifecycle

| Method / notification | Client | Server | Tests |
|---|---|---|---|
| `initialize` request | ✅ `McpClient::connectAndInitialize` | ✅ `McpServer` handler | `tst_McpLoopback.FullHandshakeListAndCallTool`, manual probe against Qt Creator |
| `notifications/initialized` | ✅ emitted after init response | ✅ accepted no-op | Loopback |
| Protocol version negotiation | ✅ | ✅ `McpServer.cpp` — list-based negotiation across all four known revisions | Manual probe |
| Shutdown | n/a | n/a | MCP does not define an explicit `shutdown` RPC — sessions end by closing the transport. We do that in `McpClient::shutdown()` and `McpServer::stop()`. |

### Authorization

| Feature | Status |
|---|---|
| OAuth 2.1 / OIDC Discovery / Dynamic Client Registration / OAuth Client ID Metadata Documents / `WWW-Authenticate` scoped consent | ❌ Not implemented. Static HTTP headers via `HttpTransportConfig::headers` only. |
| Stdio transport: retrieve credentials from environment | ✅ by convention — `StdioLaunchConfig::environment` is user-controlled. |

### JSON Schema dialect

| Requirement | Status |
|---|---|
| Default dialect JSON Schema 2020-12 | ✅ Our tool/resource schemas use plain `type`/`properties`/`required`/`enum`, valid in draft-7 through 2020-12 alike. We never set `$schema` explicitly. |
| Support at least 2020-12 | ✅ For the subset of features we use. |
| Validate schemas by declared or default dialect | 🟡 We forward schemas verbatim to the LLM provider. Per SEP-1303 guidance, input validation errors should become tool execution errors, not protocol errors, and we surface them via `isError: true`. |

### `_meta` field

| Requirement | Status |
|---|---|
| Round-trip unknown `_meta` values | ✅ Preserved verbatim on `ToolInfo`, `ResourceInfo`, `ResourceTemplate`, `PromptInfo`, and `ServerCapabilities::extras`. `McpTypes` tests cover the tool case end-to-end. |
| Respect reserved prefix rules (`modelcontextprotocol`/`mcp`) | 🟡 Documentation-only — we don't inject our own `_meta` entries anywhere, so there's nothing to collide. |

### `icons` (new in 2025-11-25)

✅ `IconInfo` struct in `include/LLMCore/McpTypes.hpp`, round-tripped on
`Implementation`, `ToolInfo`, `ResourceInfo`, `ResourceTemplate`, and `PromptInfo`.
`McpServer::tools/list` surfaces each tool's `BaseTool::displayName()` as `title`
when it differs from the id; loading icon binaries into `IconInfo::src` (as a
`data:` URI) is left to the host application.

---

## 2. Transports

### Stdio transport

| Feature | Client | Server |
|---|---|---|
| Newline-delimited JSON framing | ✅ `source/mcp/McpLineFramer.hpp` | ✅ same |
| UTF-8, compact JSON, one object per line | ✅ | ✅ |
| Stderr reserved for logging | ✅ | ✅ |
| `QProcess` launch on client, with env + cwd + startup timeout | ✅ `McpStdioClientTransport` | n/a |
| Windows `.cmd`/`.bat`/`npx` auto-wrap via `cmd.exe /c` | ✅ | n/a |
| Windows stdin reading without FILE* buffering stall (uses `_read` on fd) | n/a | ✅ |
| POSIX stdin reading via `::read(fileno(stdin))` | n/a | ✅ |
| Graceful child process shutdown (kill + wait) | ✅ | n/a |
| `LLMCORE_MCP_TRACE` env var → append-mode tracefile for debugging | n/a | ✅ |

### Streamable HTTP (2025-03-26, 2025-06-18, 2025-11-25)

| Feature | Client | Server |
|---|---|---|
| POST JSON-RPC to single endpoint | ✅ `McpHttpTransport::postV2025` | ✅ `McpHttpServerTransport` (manual HTTP/1.1 over `QTcpServer`, no `Qt6::HttpServer` dep) |
| `Accept: application/json, text/event-stream` | ✅ | ✅ |
| Parse JSON response | ✅ | ✅ `application/json` for single responses |
| Parse SSE response (short-lived, per-POST) | ✅ Uses internal `SseEventParser` | ✅ Used when flushing queued server→client messages alongside the response |
| `Mcp-Session-Id` header tracking | ✅ | ✅ Generated as a UUIDv4 at `start()`, echoed on every response, rejects a mismatched id with HTTP 400 |
| 202 Accepted with empty body | ✅ Treated as notification ack | ✅ Replied for inbound notifications when no queued server messages |
| Long-lived `GET /mcp` server-to-client push | ❌ Explicitly out of scope for v1 (client). | ❌ Server replies HTTP 405 Method Not Allowed for non-POST. Spontaneous server→client traffic is instead **buffered and flushed on the next inbound POST's response** via SSE — workable for sampling round-trips, insufficient for purely spontaneous notifications while no client is polling. |
| Polling SSE / resumption via GET / event-id encoding (2025-11-25) | ❌ Depends on the long-lived GET stream we skip. | ❌ Same. |
| HTTP 403 Forbidden for invalid Origin headers (2025-11-25 requirement) | n/a | ✅ Enforced when `HttpServerConfig::allowedOrigins` is non-empty. Empty list = accept any (local-dev default). |
| Tests | — | ✅ `tst_McpHttpServer.HandshakeAndToolCallOverHttp` — full initialize + tools/list + tools/call round-trip via `McpHttpTransport` → `McpHttpServerTransport` over TCP loopback. |

### Legacy HTTP+SSE (2024-11-05)

| Feature | Client | Server |
|---|---|---|
| Long-lived `GET /sse` event stream | ✅ `McpHttpTransport::startV2024` | — |
| Parse `event: endpoint` | ✅ | — |
| Queue outbound messages until endpoint is resolved | ✅ | — |
| POST to announced endpoint | ✅ | — |
| Parse `event: message` back via SSE | ✅ | — |
| Verified against Qt Creator 19.0.0 MCP server | ✅ `example/mcp-http-probe` + manual run |

### In-process pipe transport

| Feature | Status |
|---|---|
| `McpPipeTransport::createPair` for loopback tests and embedded server+client | ✅ |
| Queued signal/slot delivery (thread-safe via Qt event loop) | ✅ |

---

## 3. Server features

### Tools

| Method / notification | Client | Server | Tests |
|---|---|---|---|
| `tools/list` | ✅ `McpClient::listTools` | ✅ `McpServer` handler | `tst_McpLoopback.FullHandshakeListAndCallTool` |
| `tools/call` | ✅ `McpClient::callTool` | ✅ | Same + end-to-end via `McpRemoteTool` |
| `notifications/tools/list_changed` | ✅ Client emits `toolsChanged` signal | ✅ Server emits on `addTool`/`removeTool` | `tst_McpLoopback.ToolsChangedNotificationRefreshesBinding` |
| Server declares `tools.listChanged: true` capability | — | ✅ Unconditional | — |
| Tool `name`, `description`, `inputSchema` | ✅ `ToolInfo` struct | ✅ | tst_McpTypes round-trip |
| Tool `annotations` | 🟡 Preserved on `ToolInfo`, not actively read | 🟡 Preserved | |
| Tool `outputSchema` (2025-06-18+) | ✅ Field on `ToolInfo` (`outputSchema`). `BaseTool::executeAsync` returns `LLMCore::ToolResult` with an optional `structuredContent` payload; server-side serialises it; no schema-driven validation yet. | | |
| Tool `title` field (2025-11-25) | ✅ `ToolInfo::title`. `McpServer::tools/list` populates it from `BaseTool::displayName()` when it differs from `id`. | ✅ | `tst_McpTypes.ToolInfoRoundTripWithTitleIconsAndMeta` |
| Tool `icons` (2025-11-25) | ✅ `ToolInfo::icons` round-trips | ✅ | Same test |
| Tool `_meta` | ✅ Round-tripped on `ToolInfo::meta` | ✅ | Same test |
| `isError: true` content block | ✅ `BaseTool` returns `ToolResult::error(msg)`. Server preserves the flag; `ToolHandler` flattens errored results onto `toolFailed`. |
| Tool-call content types: text | ✅ `ToolContent::Text` |
| Tool-call content types: image | ✅ Base64 wire encoding; end-to-end for Claude, OpenAI Responses (via `input_image` blocks in `function_call_output.output`), and Google Gemini 3-series (via `functionResponse.parts[].inlineData`) |
| Tool-call content types: audio (2025-03-26+) | ✅ `ToolContent::Audio` — also flows through Gemini's `inlineData` parts when the ToolResult carries audio |
| Tool-call content types: resource | ✅ `makeResourceText` / `makeResourceBlob` |
| Tool-call content types: resource_link (2025-06-18+) | ✅ `makeResourceLink` |
| `structuredContent` payload (2025-06-18+) | ✅ `ToolResult::structuredContent` |

**BaseTool adapter pattern** — every `McpRemoteTool` is a drop-in `BaseTool`, so any
`LLMCore::BaseClient` (Claude, OpenAI, Ollama, Google, LlamaCpp) transparently consumes
MCP tools via the existing `ToolsManager` multi-turn loop. See
`include/LLMCore/McpRemoteTool.hpp` and the `McpToolBinder` helper.

**ToolResult** (`include/LLMCore/ToolResult.hpp`) is the canonical rich tool output
type for the whole library.

### Resources

| Method / notification | Client | Server | Tests |
|---|---|---|---|
| `resources/list` | ✅ `McpClient::listResources` | ✅ Aggregates across every registered `BaseResourceProvider` | `tst_McpLoopback.ResourcesListAndRead` |
| `resources/read` | ✅ `McpClient::readResource` | ✅ Tries each provider in order, first hit wins | Same |
| `resources/templates/list` (URI-templated resources) | ✅ `McpClient::listResourceTemplates` | ✅ `BaseResourceProvider::listResourceTemplates` virtual (default: empty list). Aggregates across all providers. | `tst_McpLoopback.ResourceTemplatesListRoundTrips` |
| `resources/subscribe` | ✅ `McpClient::subscribeResource` | ✅ Delegated to providers that declare `supportsSubscription()` | — |
| `resources/unsubscribe` | ✅ `McpClient::unsubscribeResource` | ✅ | — |
| `notifications/resources/list_changed` | ✅ Client emits `resourcesChanged` signal | ✅ Server forwards from provider's `listChanged` signal | — |
| `notifications/resources/updated` | ✅ Client emits `resourceUpdated(uri)` signal | ✅ Server forwards from provider's `resourceUpdated` signal | — |
| Server declares `resources.listChanged` / `resources.subscribe` capabilities | — | ✅ Conditional on provider presence + subscription support | — |
| Resource `title` / `icons` / `_meta` (2025-11-25) | ✅ Round-tripped on `ResourceInfo` and `ResourceTemplate` | ✅ | `tst_McpTypes.ResourceTemplateAndRootRoundTrip` |
| Resource content types: text | ✅ `ResourceContents::text` |
| Resource content types: blob (base64) | ✅ `ResourceContents::blob`, serialised as base64 on the wire |

### Prompts

✅ **Full CRUD coverage** via `BasePromptProvider`.

| Method / notification | Client | Server | Tests |
|---|---|---|---|
| `prompts/list` | ✅ `McpClient::listPrompts` | ✅ Aggregates across every registered `BasePromptProvider` | `tst_McpLoopback.PromptsListAndGet` |
| `prompts/get` (with argument substitution) | ✅ `McpClient::getPrompt` | ✅ `BasePromptProvider::getPrompt` returns a `PromptGetResult` | Same |
| `notifications/prompts/list_changed` | ✅ Client emits `promptsChanged` signal | ✅ Server forwards from provider's `listChanged` signal | Direct type-test coverage |
| Server declares `prompts.listChanged` capability | — | ✅ Conditional on provider presence | `tst_McpTypes.ServerCapabilitiesPreservePromptsAndLogging` |
| Prompt arguments (name, description, required) | ✅ `PromptArgument` struct | ✅ | `tst_McpTypes.PromptInfoAndGetResultRoundTrip` |
| Prompt messages (role + content block) | ✅ `PromptMessage` struct | ✅ | Same |
| Prompt `title` / `icons` / `_meta` | ✅ Round-tripped | ✅ | Same |

`BasePromptProvider` lives in `include/LLMCore/BasePromptProvider.hpp`; subclass it,
override `listPrompts()` and `getPrompt()`, then register with
`McpServer::addPromptProvider()`. Report missing-prompt / bad-argument conditions by
throwing `McpRemoteError(ErrorCode::InvalidParams, ...)` from inside the future.

### Completion

✅ **Full coverage** for prompts and resource URI templates.

| Feature | Status |
|---|---|
| `completion/complete` request handler | ✅ `McpServer` dispatches by `ref.type` (`ref/prompt` / `ref/resource`) across every registered `BasePromptProvider` / `BaseResourceProvider` and merges their results. |
| `completions` capability advertised during `initialize` | ✅ Flagged whenever at least one prompt or resource provider is registered. |
| `BasePromptProvider::completeArgument(promptName, argumentName, partialValue, contextArguments)` virtual | ✅ Default returns empty list — advisory API, no errors. |
| `BaseResourceProvider::completeArgument(templateUri, placeholderName, partialValue, contextArguments)` virtual | ✅ Same default semantics. |
| Client helper `McpClient::complete(ref, argumentName, partialValue, contextArguments)` | ✅ Returns `CompletionResult { values, total?, hasMore }`. |
| `context.arguments` field for dependent-value completion (2025-06-18+) | ✅ Forwarded verbatim to providers. |
| Tests | ✅ `tst_McpTypes.CompletionTypesRoundTrip`, `ServerCapabilitiesCompletionsFlag`; `tst_McpLoopback.CompletionCapabilityAdvertisedWithProviders`, `CompletionForPromptArgumentReturnsFilteredSuggestions`, `CompletionForResourceTemplatePlaceholder`, `CompletionDefaultProviderReturnsEmptyList`. |

Note: completion is deliberately advisory — provider implementations that
cannot answer should return an empty `CompletionResult` rather than throw.
Errors from providers are swallowed by the server handler for the same reason,
so a broken completer never crashes the calling client's UI.

### Logging

✅ **Full coverage.**

| Feature | Status |
|---|---|
| `logging/setLevel` request (client → server) | ✅ Handler in `McpServer` stores the level in `McpServer::currentLogLevel()` and emits `logLevelChanged(level)` signal. Default: `"info"`. |
| `notifications/message` (server → client) | ✅ `McpServer::sendLogMessage(level, logger, data, message)` emits the notification; `McpClient::logMessage(level, logger, data, message)` signal fires on the client side. |
| `logging` capability advertised during `initialize` | ✅ Controlled by `McpServerConfig::advertiseLogging` (default true). |
| Standard log levels (debug/info/notice/warning/error/critical/alert/emergency) | ✅ Constants in `LLMCore::Mcp::LogLevel` namespace. |
| Tests | ✅ `tst_McpLoopback.SetLogLevelIsRecordedOnServer`, `LogMessageNotificationFromServerToClient` |

Note: Qt's `QLoggingCategory` system (`llmMcpLog`, enable with
`QT_LOGGING_RULES="llmcore.mcp*=true"`) remains the primary debug/diagnostic channel
for library internals; the MCP logging channel is for application-level logs that
the host wants to expose to the connected client.

---

## 4. Client features

| Feature | Status | Notes |
|---|---|---|
| Roots — `roots/list`, `notifications/roots/list_changed` | ✅ | `BaseRootsProvider` (`include/LLMCore/BaseRootsProvider.hpp`): subclass, override `listRoots()`, emit `listChanged()` when the set changes. Register with `McpClient::setRootsProvider()`. Handler installed by default; returns an empty list when no provider is set. Declares `roots.listChanged: true` capability during `initialize` if a provider is present. Tests: `tst_McpLoopback.RootsListRoundTripsFromServerToClient`. |
| Sampling — `sampling/createMessage` | ✅ | Full end-to-end. `McpClient::setSamplingClient(BaseClient*, SamplingPayloadBuilder)` wires a live `BaseClient` as the executor for incoming `sampling/createMessage` requests. The library drives `BaseClient::sendMessage()` through the host-supplied payload builder, collects the finalised result via the new `RequestCallbacks::onFinalized` callback (`CompletionInfo{fullText, model, stopReason}`), and wraps it into `CreateMessageResult` automatically. The `sampling` capability is declared during `initialize` when a sampling client is wired. Without it, incoming `sampling/createMessage` requests reply with `MethodNotFound`. Host builder lambdas own all provider-specific JSON serialisation — LLMCore never chases per-provider API evolution. **Tool-calling-inside-sampling (2025-11-25) comes for free** via the existing `ToolsManager` continuation loop: whatever tools (local `BaseTool` + remote `McpRemoteTool` from other servers) are registered on the `BaseClient` are available to the sampling call, and `onFinalized` fires only after the full multi-turn loop converges. `McpServer::createSamplingMessage()` is the server-side initiator, with a capability guard that short-circuits with `McpProtocolError` if the peer never advertised `sampling`. Types (`SamplingMessage`, `ModelHint`, `ModelPreferences`, `CreateMessageParams`, `CreateMessageResult`) round-trip in `tst_McpTypes`; end-to-end happy path, capability guard, and client error propagation are covered in `tst_McpLoopback.Sampling*` using a `FakeSamplingClient` (a minimal `BaseClient` stub with canned completion). |
| Elicitation — `elicitation/create` (new in 2025-06-18, URL mode added 2025-11-25) | 🟡 | Protocol plumbing complete, symmetric to Sampling: `BaseElicitationProvider` (`include/LLMCore/BaseElicitationProvider.hpp`) is the abstract host-implemented hook; `McpClient::setElicitationProvider()` installs it, declares the `elicitation` capability during `initialize`, and dispatches incoming `elicitation/create` requests (replies with `MethodNotFound` when no provider is set). `McpServer::createElicitation()` is the server-side initiator with the same capability-guarded short-circuit as sampling. Types (`ElicitRequestParams` with `mode`/`url` for 2025-11-25 URL mode, `ElicitResult`, `ElicitationCapability`, `ElicitAction` constants) round-trip in `tst_McpTypes`; end-to-end happy path, capability guard, and provider refusal are covered in `tst_McpLoopback.Elicitation*`. **No UI is bundled** — the host decides whether to render a modal, embed a chat form, open a URL (URL mode), validate user input against `requestedSchema`, and whether to return `accept` / `decline` / `cancel`. See `BaseElicitationProvider.hpp` for the expected shape. |
| Tool calling inside sampling (`tools`/`toolChoice` params, new in 2025-11-25) | 🟡 | **Execution side done**: the `BaseClient` + `ToolsManager` loop runs tools transparently during a sampled call (see Sampling row above). **Parameter side not plumbed**: the `tools` / `toolChoice` fields a server can send in `sampling/createMessage` are parked in `CreateMessageParams::metadata` verbatim and must be lifted into the provider payload by the host's `SamplingPayloadBuilder` lambda. Follow-up: surface them as typed fields on `CreateMessageParams`. |

---

## 5. Utilities

| Utility | Status | Details |
|---|---|---|
| **Ping** (`ping`) | ✅ | `McpClient::ping(timeout)` returns `QFuture<void>`. Server- and client-side handlers installed by default. Tests: `tst_McpLoopback.PingRoundTrips`. |
| **Cancellation** (`notifications/cancelled`) | ✅ | `McpClient::callToolWithProgress()` returns a `CancellableToolCall` struct with a `requestId`. Call `McpClient::cancel(requestId, reason)` (or `session()->cancelRequest()` directly) to send `notifications/cancelled { requestId, reason? }` to the peer and complete the local future with `McpCancelledError`. On the server side, `McpSession` marks the in-flight incoming id on cancellation and suppresses the final response; request handlers can poll `session()->isRequestCancelled(id)` between expensive steps. Tests: `tst_McpLoopback.CancelRequestAbortsOutstandingCall`. |
| **Progress** (`notifications/progress`) | ✅ | Built on progress tokens carried in `params._meta.progressToken`. `McpSession::sendCancellableRequest()` tags outbound requests with a token equal to the request id and routes `notifications/progress` back to the caller's registered `ProgressHandler`. Server-side request handlers call `session()->sendProgress(token, progress, total, message)` using `session()->currentProgressToken()` captured inside the handler. `McpClient::callToolWithProgress(name, args, onProgress)` is a convenience wrapper that registers and clears the handler around the call. Tests: `tst_McpLoopback.ProgressNotificationsDeliveredToCallback`. |
| **Tasks** (experimental 2025-11-25) | ❌ | Durable / pollable / deferred tool invocations. Out of scope for MVP. |
| **Error reporting** | ✅ | JSON-RPC error envelopes; exceptions in handlers are caught and serialised. `McpCancelledError` maps to `-32800 RequestCancelled`. |
| **Argument completion** | ❌ | See Completion above. |

---

## 6. Testing coverage

| Scope | Where |
|---|---|
| `McpLineFramer` (partial reads, CRLF, UTF-8 boundaries, empty lines) | `tests/tst_McpLineFramer.cpp` (7 cases) |
| `McpTypes` JSON round-trips | `tests/tst_McpTypes.cpp` — `Implementation`, `ToolInfo` (incl. title/icons/_meta), `InitializeResult`, `ResourceContents` text + blob, `ResourceTemplate`, `Root`, `PromptInfo`, `PromptGetResult`, `ServerCapabilities` (prompts + logging + completions), `ClientCapabilities` (roots + sampling + elicitation), `CompletionReference`/`CompletionArgument`/`CompletionResult`, `SamplingMessage`/`ModelHint`/`ModelPreferences`/`CreateMessageParams`/`CreateMessageResult`, `ElicitRequestParams` (form + url mode) / `ElicitResult` (accept + decline) — **20 cases** |
| `ToolResult` factories, `asText()` flattening, content block round-trips, full envelope round-trip with `structuredContent` and `isError` | `tests/tst_ToolResult.cpp` (18 cases) |
| End-to-end loopback: handshake, tools/list + call, tools/list_changed, `McpToolBinder`, resources/list + read, resources/templates/list, prompts/list + get, roots/list (server→client), ping, logging/setLevel + notifications/message, progress, cancellation, completion/complete (prompt, resource template, default empty, capability advertisement), sampling/createMessage (happy path, MethodNotFound guard, provider refusal), elicitation/create (happy path, capability guard, provider refusal) | `tests/tst_McpLoopback.cpp` — **22 cases** via `McpPipeTransport` |
| Server-side HTTP hosting round-trip (handshake + tools/list + tools/call) | `tests/tst_McpHttpServer.cpp` — **1 case** pairing `McpHttpTransport` against `McpHttpServerTransport` over TCP loopback |
| Real stdio transport (Windows `QProcess` path) | Manual: `mcp_probe_*.jsonl` piped into `example-mcp-server.exe` |
| Real HTTP+SSE transport (`2024-11-05`) | Manual: `example-mcp-http-probe --spec 2024-11-05 http://127.0.0.1:3001/sse` against Qt Creator 19.0.0 MCP server |
| Chat example end-to-end (MCP tools appear alongside local tools in any provider's `ToolsManager`) | `example/example-chat.exe` + `example/mcp-servers.json` |
| Self-hosted MCP server under `claude mcp add` | Verified against Claude Code CLI in this repo during development |

**Integration tests** (`tests/integration/tst_ClaudeMcpIntegration.cpp`) — ✅
`ClaudeMcpIntegrationTest` runs a real `ClaudeClient` against an in-process
`McpServer` over `McpPipeTransport`, covering tool discovery
(`ListsToolsFromMcpServerIntoToolsManager`), echo call-through
(`ClaudeCallsMcpEchoTool`), and a numeric calculator round-trip
(`ClaudeCallsMcpCalculatorTool`). Verified against `claude-sonnet-4-6` on
2026-04-11.

**Total**: 279 unit tests in the full suite, 43 of which are MCP-specific (20
`McpTypesTest` + 22 `McpLoopbackTest` + 1 `McpHttpServerTest`, post-refactor
rewrite moved the sampling loopback trio from `BaseSamplingProvider` subclass
mocks to a `FakeSamplingClient : BaseClient` stub driven through
`McpClient::setSamplingClient`) plus the 18 `ToolResultTest` cases, 31
`OpenAIResponsesMessage` cases (3 new: image/text/audio tool-result block
rendering), and 40 `GoogleMessage` cases (3 new:
image/audio/text-only tool-result parts). Integration tests gated on API
keys: 3 Claude+MCP (`CLAUDE_API_KEY`), 1 OpenAI Responses image-tool
(`OPENAI_API_KEY`), and 1 Google Gemini image-tool (`GOOGLE_API_KEY`).

*Last reviewed: 2026-04-12 against MCP specification 2025-11-25.*
