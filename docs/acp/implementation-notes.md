# ACP host — implementation notes (as built)

Status: **implemented, host/client side.** This documents what actually landed and
where it differs from the design in the sibling docs. Source of truth for class names
and file locations.

## File map

| Area | Public header | Source |
|---|---|---|
| Shared JSON-RPC session (`Rpc`) | [`include/LLMQore/JsonRpcSession.hpp`](../../include/LLMQore/JsonRpcSession.hpp) | [`source/rpc/JsonRpcSession.cpp`](../../source/rpc/JsonRpcSession.cpp) |
| Shared transport + framing (`Rpc`) | [`RpcTransport.hpp`](../../include/LLMQore/RpcTransport.hpp), [`RpcStdioTransport.hpp`](../../include/LLMQore/RpcStdioTransport.hpp), [`RpcPipeTransport.hpp`](../../include/LLMQore/RpcPipeTransport.hpp) | `source/rpc/*.cpp` |
| Shared JSON-RPC errors (`Rpc`) | [`include/LLMQore/RpcExceptions.hpp`](../../include/LLMQore/RpcExceptions.hpp) | — |
| MCP session (compat subclass) | [`include/LLMQore/McpSession.hpp`](../../include/LLMQore/McpSession.hpp) | — (header-only) |
| ACP wire types | [`include/LLMQore/AcpTypes.hpp`](../../include/LLMQore/AcpTypes.hpp) | [`source/acp/AcpTypes.cpp`](../../source/acp/AcpTypes.cpp) |
| Host driver | [`include/LLMQore/AcpClient.hpp`](../../include/LLMQore/AcpClient.hpp) | [`source/acp/AcpClient.cpp`](../../source/acp/AcpClient.cpp) |
| Permission provider (interface) | [`include/LLMQore/AcpPermissionProvider.hpp`](../../include/LLMQore/AcpPermissionProvider.hpp) | — |
| Permission provider (callback impl) | [`include/LLMQore/CallbackPermissionProvider.hpp`](../../include/LLMQore/CallbackPermissionProvider.hpp) | — (header-only) |
| FS provider (interface) | [`include/LLMQore/AcpFileSystemProvider.hpp`](../../include/LLMQore/AcpFileSystemProvider.hpp) | — |
| FS provider (QFile impl) | [`include/LLMQore/DefaultFileSystemProvider.hpp`](../../include/LLMQore/DefaultFileSystemProvider.hpp) | [`source/acp/DefaultFileSystemProvider.cpp`](../../source/acp/DefaultFileSystemProvider.cpp) |
| Terminal provider (interface) | [`include/LLMQore/AcpTerminalProvider.hpp`](../../include/LLMQore/AcpTerminalProvider.hpp) | — |
| Terminal provider (QProcess impl) | [`include/LLMQore/TerminalManager.hpp`](../../include/LLMQore/TerminalManager.hpp) | [`source/acp/TerminalManager.cpp`](../../source/acp/TerminalManager.cpp) |
| Agent launch config | [`include/LLMQore/AcpAgentConfig.hpp`](../../include/LLMQore/AcpAgentConfig.hpp) | [`source/acp/AcpAgentConfig.cpp`](../../source/acp/AcpAgentConfig.cpp) |
| Agent catalogue (JSON-loaded) | [`include/LLMQore/AcpAgentRegistry.hpp`](../../include/LLMQore/AcpAgentRegistry.hpp) | [`source/acp/AcpAgentRegistry.cpp`](../../source/acp/AcpAgentRegistry.cpp) |
| GUI example (ACP path) | — | [`example/ChatController.cpp`](../../example/ChatController.cpp) + [`example/Main.qml`](../../example/Main.qml) |
| Tests | — | `tst_AcpTypes.cpp`, `tst_AcpLoopback.cpp`, `tst_AcpProviders.cpp`, `tst_AcpAgentConfig.cpp`, `tst_AcpAgentRegistry.cpp` |

ACP code lives in `LLMQore::Acp`; the shared JSON-RPC plumbing lives in `LLMQore::Rpc`.

## Layering (as built)

A self-contained `LLMQore::Rpc` layer carries the shared JSON-RPC plumbing; the MCP and
ACP stacks both build on it.

- `Rpc::Transport` (abstract), `Rpc::StdioClientTransport`, `Rpc::PipeTransport`,
  `Rpc::LineFramer` — byte-level transports + framing.
- `Rpc::JsonRpcSession` — request/response/notification dispatch, timeouts, progress,
  cancellation.
- `Rpc::ErrorCode` + the exception hierarchy `Rpc::JsonRpcException`, `RemoteError`,
  `TransportError`, `TimeoutError`, `CancelledError`, `ProtocolError`.

ACP (`AcpClient`, `AcpAgentConfig`, the providers) references `Rpc::` names directly.
The MCP stack keeps its historical spellings via thin compatibility aliases so **no MCP
source or test changed**:

| MCP name | is now an alias of |
|---|---|
| `Mcp::McpTransport` | `Rpc::Transport` |
| `Mcp::McpSession` | subclass of `Rpc::JsonRpcSession` |
| `Mcp::McpStdioClientTransport` / `StdioLaunchConfig` | `Rpc::StdioClientTransport` / `Rpc::StdioLaunchConfig` |
| `Mcp::McpPipeTransport` | `Rpc::PipeTransport` |
| `Mcp::McpRemoteError` / `McpException` / … | `Rpc::RemoteError` / `Rpc::JsonRpcException` / … |
| `Mcp::ErrorCode` | `Rpc::ErrorCode` |

MCP-specific transports (`McpStdioServerTransport`, `McpHttpTransport`,
`McpHttpServerTransport`) stay in `Mcp` and inherit `Rpc::Transport`.

Two design choices to note:

- **Progress / cancellation live in `Rpc::JsonRpcSession`.** They are generic JSON-RPC
  patterns (MCP standardised the `notifications/progress` / `notifications/cancelled`
  method names) that an ACP agent simply never triggers.
- **Permission has no capability flag.** `session/request_permission` is always handled;
  with no `AcpPermissionProvider` the host answers `outcome: cancelled`. Only `fs.*` and
  `terminal` are gated by provider presence in `initialize`.

## Method coverage

| ACP method | Direction | Status |
|---|---|---|
| `initialize` | host → agent | ✓ `AcpClient::connectAndInitialize` |
| `authenticate` | host → agent | ✓ `AcpClient::authenticate` |
| `session/new` | host → agent | ✓ `AcpClient::newSession` |
| `session/load` | host → agent | ✓ `AcpClient::loadSession` |
| `session/prompt` | host → agent | ✓ `AcpClient::prompt` |
| `session/cancel` | host → agent (notify) | ✓ `AcpClient::cancel` |
| `session/set_mode` | host → agent | ✓ `AcpClient::setMode` |
| `session/update` | agent → host (notify) | ✓ routed to typed signals |
| `session/request_permission` | agent → host | ✓ `AcpPermissionProvider` |
| `fs/read_text_file` | agent → host | ✓ `AcpFileSystemProvider` |
| `fs/write_text_file` | agent → host | ✓ `AcpFileSystemProvider` |
| `terminal/create` | agent → host | ✓ `AcpTerminalProvider` |
| `terminal/output` | agent → host | ✓ `AcpTerminalProvider` |
| `terminal/wait_for_exit` | agent → host | ✓ `AcpTerminalProvider` |
| `terminal/kill` | agent → host | ✓ `AcpTerminalProvider` |
| `terminal/release` | agent → host | ✓ `AcpTerminalProvider` |

`session/update` variants handled: `user_message_chunk`, `agent_message_chunk`,
`agent_thought_chunk`, `tool_call`, `tool_call_update` (merged by `toolCallId`),
`plan`, `available_commands_update`, `current_mode_update`, `usage_update` (raw JSON via
the `AcpClient::usageUpdated` signal).

## Live validation

Validated end-to-end against **Claude Code** via the `@agentclientprotocol/claude-agent-acp`
adapter (the renamed `@zed-industries/claude-code-acp`):
`initialize` → `session/new` → `session/prompt` → streamed `agent_message_chunk` →
`stopReason: "end_turn"`, driven by an `AcpClient` host (the `example-chat` ACP path).
Two corrections came out of that run and are folded in:

- **`stopReason` values** are `end_turn` / `max_tokens` / `max_turn_requests` /
  `refusal` / `cancelled` — not the `Completed` / `Error` an earlier schema summary
  implied. `StopReason::*` and the loopback tests now use these.
- **`usage_update`** is a real `session/update` variant (token accounting), surfaced via
  the `AcpClient::usageUpdated(sessionId, rawJson)` signal.

## Still unvalidated (no live trigger yet)

- **`session/request_permission` outcome shape** —
  `{ "outcome": { "outcome": "selected" | "cancelled", "optionId"? } }`; the hello-world
  prompt didn't trigger a permission request, so confirm against a tool-using turn.
- **`fs/*` and `terminal/*`** — exercised only by the loopback tests so far.
- **Agent catalogue** — agents are data, loaded by `AcpAgentRegistry` from external JSON
  ([`example/agents.json`](../../example/agents.json)); the library ships no built-in
  agents. The `claude` entry is live-verified; `gemini`/`codex` package names/flags are
  best-effort.
- **`protocolVersion`** — sent as integer `1`; bump as the negotiated version evolves.

## Minimal usage

```cpp
using namespace LLMQore;
using namespace LLMQore::Acp;

AcpAgentRegistry registry;
registry.loadFromFile("agents.json");            // or ":/agents.json"
auto cfg = registry.config("claude", QDir::currentPath()).value();
auto *client = new AcpClient(cfg.createTransport(parent), {}, parent);
client->setFileSystemProvider(new DefaultFileSystemProvider(parent));
client->setTerminalProvider(new TerminalManager(parent));
client->setPermissionProvider(new CallbackPermissionProvider(myPolicy, parent));

QObject::connect(client, &AcpClient::agentMessageChunk,
    [](const QString &sid, const ContentBlock &c){ /* render c.text */ });

compat(client->connectAndInitialize())
    .then(client, [client](const InitializeResult &){ return client->newSession({/*cwd*/}); })
    .unwrap()
    .then(client, [client](const NewSessionResult &ns){
        return client->prompt(ns.sessionId, {ContentBlock::makeText("Hello")}); })
    .unwrap();
```

See the ACP path in [`example/ChatController.cpp`](../../example/ChatController.cpp)
(`setupAcpAgent` / `send`) for a GUI host — pick the **Claude Code (ACP)** provider in
`example-chat`.

## Tests

`tst_AcpTypes` (round-trip of every wire struct), `tst_AcpLoopback` (handshake,
streaming, cancel, and a full prompt turn where the agent calls back into the host for
`fs/read_text_file` + `session/request_permission`), `tst_AcpProviders`
(`DefaultFileSystemProvider`, `TerminalManager`, `CallbackPermissionProvider`),
`tst_AcpAgentConfig` (launch config + JSON round-trip), `tst_AcpAgentRegistry`
(catalogue load/merge/lookup). All run under the standard
`LLMQoreUnitTests` target with no network or external processes (the terminal test runs
`echo`).
