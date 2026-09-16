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
| Tests (unit) | — | `tst_AcpTypes.cpp`, `tst_AcpLoopback.cpp`, `tst_AcpProviders.cpp`, `tst_AcpAgentConfig.cpp`, `tst_AcpAgentRegistry.cpp` |
| Tests (conformance) | — | [`tests/integration/tst_AcpIntegration.cpp`](../../tests/integration/tst_AcpIntegration.cpp) |

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

Both stacks reference the `Rpc::` names directly — the historical `Mcp::McpTransport`
/ `Mcp::McpSession` / `Mcp::McpStdioClientTransport` spellings were removed in the #27
refactor with no compatibility aliases. The renames were:

| Removed MCP name | replaced by |
|---|---|
| `Mcp::McpTransport` | `Rpc::Transport` |
| `Mcp::McpSession` | `Rpc::JsonRpcSession` |
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
| `session/set_config_option` | host → agent | ✓ `AcpClient::setConfigOption` |
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
`plan`, `available_commands_update`, `current_mode_update`, `config_option_update`,
`usage_update` (raw JSON via the `AcpClient::usageUpdated` signal).

## Live validation

Validated end-to-end against **Claude Code** via the `@agentclientprotocol/claude-agent-acp`
adapter (the renamed `@zed-industries/claude-code-acp`):
`initialize` → `session/new` → `session/prompt` → streamed `agent_message_chunk` →
`stopReason: "end_turn"`, driven by an `AcpClient` host (the `example-chat` ACP path).

This is now automated: [`tests/integration/tst_AcpIntegration.cpp`](../../tests/integration/tst_AcpIntegration.cpp)
launches the adapter with `npx` and runs nine conformance cases against it. Build with
`-DLLMQORE_BUILD_INTEGRATION_TESTS=ON` and run
`LLMQoreIntegrationTests --gtest_filter='AcpIntegrationTest.*'` (~20 s, real tokens).

Corrections that came out of these runs and are folded in:

- **`stopReason` values** are `end_turn` / `max_tokens` / `max_turn_requests` /
  `refusal` / `cancelled` — not the `Completed` / `Error` an earlier schema summary
  implied. `StopReason::*` and the loopback tests now use these.
- **`usage_update`** is a real `session/update` variant (token accounting), surfaced via
  the `AcpClient::usageUpdated(sessionId, rawJson)` signal.
- **Auth is the agent's job.** Claude's adapter advertises no `authMethods` and reads its
  credential from the environment (`CLAUDE_CODE_OAUTH_TOKEN` / `ANTHROPIC_API_KEY`) or the
  macOS Keychain. A GUI-launched host must pass a token explicitly — see
  [`authentication.md`](authentication.md).
- **Mode changes arrive on the other API's channel.** Against the Claude adapter,
  `session/set_mode` answers `{}` and reports the change as `config_option_update`, so
  `configOptionsUpdated` fires. `session/set_config_option` on the `mode` option is the
  reverse: the reply carries the updated `configOptions` and the agent emits
  `current_mode_update`, so `modeChanged` fires. Neither signal is synthesised on our
  side. Hosts should read the `set_config_option` reply and connect both signals; the
  Codex adapter sends no notification at all after `set_config_option`.

## Unreachable surface (this agent will never exercise it)

Measured against `@agentclientprotocol/claude-agent-acp` 0.64.0 (ACP SDK 1.2.1) over a
full tool-using turn. The adapter called exactly **one** host method the whole turn:
`session/request_permission`.

- **`fs/read_text_file` / `fs/write_text_file` — never called.** The Claude Agent SDK's
  Read and Write tools go to the filesystem directly. Advertising
  `clientCapabilities.fs.readTextFile` / `.writeTextFile` does not change this: in the
  adapter these two methods are pass-through forwarders required by the ACP `Agent`
  interface that nothing on the tool path invokes, and no flag or setting reroutes them.
- **`terminal/*` — never called.** The Bash tool runs in-process. The adapter's only
  `terminal` capability reference is `clientCapabilities.auth.terminal`, an auth-flow
  concern unrelated to our `terminal` flag.

Both stay covered by `tst_AcpLoopback` alone, and now actually are:
`AcpLoopbackTest.PromptDrivesHostCallbacksFsAndPermission` drives `fs/*`, and
`AcpLoopbackTest.TerminalCallsReachTheProviderAndComeBackInShape` drives all five
`terminal/*` methods plus the refusal a host without a terminal provider sends back.
This is a property of the agent, not a gap in effort: no amount of work against this adapter will close it, and a conformance test
asserting the file appeared on disk would be a false green — the agent writes it without
touching the host provider. A different agent could close it; swap one in with
`LLMQORE_ACP_AGENT_CMD` / `LLMQORE_ACP_AGENT_ARGS`.

## Validated live

- **`session/request_permission` outcome shape** — confirmed. The agent offers
  `allow_always` / `allow_once` / `reject_once` (optionIds `allow_always` / `allow` /
  `reject`), and accepts our
  `{ "outcome": { "outcome": "selected", "optionId": … } }`. The permission `toolCall`
  carries `rawInput`, `title`, `kind`, `content` (a `diff` block whose `oldText` may be
  `null`) and `locations` — but **no `status`**.
- **`protocolVersion`** — the SDK's `PROTOCOL_VERSION` is `1`, matching
  `kAcpProtocolVersion`.
- **Partial `tool_call_update` merging** — the agent sends many updates carrying only
  `toolCallId` plus one or two fields; `mergeToolCall` preserving prior `title` /
  `rawInput` across them is exercised for real.

## Known gaps in our types

- **`ClientCapabilities`** serialises `{fs, terminal, session.configOptions.boolean}`; the
  agent also reads `elicitation.form` / `elicitation.url`, which we do not send.

## Still unvalidated

- **Agent catalogue** — agents are data, loaded by `AcpAgentRegistry` from external JSON
  ([`example/agents.json`](../../example/agents.json)); the library ships no built-in
  agents. The `claude` entry is live-verified; the `codex` package name is best-effort.
  The `gemini` entry was dropped — Google discontinued the project, so its
  `--experimental-acp` invocation is dead weight in an example catalogue.

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

`tst_AcpIntegration` (conformance against a live adapter: `initialize`, `session/new`,
`session/set_mode`, a smoke turn, a cancelled turn, and a permission-bearing tool turn)
runs under `LLMQoreIntegrationTests`, gated by `LLMQORE_BUILD_INTEGRATION_TESTS`. It
launches `npx -y @agentclientprotocol/claude-agent-acp` unless overridden by
`LLMQORE_ACP_AGENT_CMD` / `LLMQORE_ACP_AGENT_ARGS`, and gives the agent a `QTemporaryDir`
sandbox — the host refuses any callback naming a path outside it. If the agent cannot be
launched the cases skip; if the agent answers with `errorKind: "authentication_failed"`
they fail, pointing at [`authentication.md`](authentication.md).

**Running from a GUI launcher.** macOS gives Finder-launched apps a bare
`/usr/bin:/bin:/usr/sbin:/sbin`, so a Qt Creator run finds neither `npx` nor the `node`
its `#!/usr/bin/env node` shebang needs. **`LLMQORE_ENV_PATH`** covers both: its entries are
prepended to the search path used to locate the agent binary *and* to the `PATH` handed to
the launched process. It is a general integration-test knob (`childSearchPath()` /
`childEnvironment()` in `IntegrationTestHelpers.hpp`), not ACP-specific — nothing about a
machine's layout is hardcoded anywhere. Without it the cases skip and print the search path
they used. `CLAUDE_CODE_OAUTH_TOKEN` has to be set the same way, since no shell profile is
sourced for a Finder-launched process.

```
LLMQORE_ENV_PATH=/opt/homebrew/bin
CLAUDE_CODE_OAUTH_TOKEN=sk-ant-oat…
```
