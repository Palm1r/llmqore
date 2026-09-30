# Shared JSON-RPC session + transport

ACP and MCP-over-stdio are the same wire mechanics: newline-delimited JSON-RPC 2.0
over a child process. Rather than duplicate that, we extract the protocol-agnostic
core of the MCP stack into a shared `LLMQore::Rpc` layer that both stacks sit on.

## What is generic

[`Rpc::JsonRpcSession`](../../../include/LLMQore/JsonRpcSession.hpp) is a generic
JSON-RPC dispatcher: `sendRequest`/`sendNotification`, request/notification handler
maps, a pending-request table with timeouts, and `sendResponse`/`sendError`. It also
carries the `notifications/cancelled` / `notifications/progress` handlers and the
`_meta.progressToken` convention — generic JSON-RPC patterns that MCP standardised
and an ACP agent simply never triggers.

[`Rpc::Transport`](../../../include/LLMQore/RpcTransport.hpp) is fully
protocol-agnostic: `start/stop/isOpen/send(QJsonObject)` plus `messageReceived`,
`errorOccurred`, `sendFailed`, `closed`. `sendFailed(message, reason)` is for a transport
that learns a request it sent will never be answered -- an HTTP POST that failed or came
back without the response; `JsonRpcSession` fails that request at once with a
`TransportError` instead of leaving it to its timer.
[`Rpc::StdioClientTransport`](../../../source/rpc/RpcStdioClientTransport.cpp)
launches a child process, frames stdout with
[`Rpc::LineFramer`](../../../include/LLMQore/RpcLineFramer.hpp), writes
`QJsonDocument(...).toJson(Compact) + "\n"`, and surfaces child stderr via its own
`stderrLine` signal. None of that is MCP-aware.

## Layering

```mermaid
flowchart TD
    subgraph Rpc["LLMQore::Rpc (shared)"]
        T["Transport (abstract)"]
        SC["StdioClientTransport"]
        SS["StdioServerTransport"]
        PIPE["PipeTransport"]
        LF["LineFramer"]
        JS["JsonRpcSession<br/><small>dispatch, pending, timeouts</small>"]
    end

    subgraph Mcp["LLMQore::Mcp"]
        MC["McpClient / McpServer<br/><small>use JsonRpcSession directly</small>"]
    end

    subgraph Acp["LLMQore::Acp"]
        AC["AcpClient<br/><small>uses JsonRpcSession directly</small>"]
    end

    T <|-- SC
    T <|-- SS
    T <|-- PIPE
    SC --> LF
    JS --> T
    MC --> JS
    AC --> JS
```

## How the layer was built (as built)

1. **`Rpc::Transport`** (abstract base, formerly `McpTransport`),
   `Rpc::StdioClientTransport` (+ `Rpc::StdioLaunchConfig`), `Rpc::PipeTransport` and
   `Rpc::LineFramer` live in `LLMQore::Rpc` (`include/LLMQore/Rpc*.hpp`, `source/rpc/`).
   The code was already protocol-neutral, so these were mechanical moves.
2. **`Rpc::JsonRpcSession`** is the full generic dispatcher. Progress
   (`notifications/progress`) and cancel-by-id (`notifications/cancelled`) stayed in the
   base — generic JSON-RPC patterns an ACP agent never triggers.
3. **The MCP stack was ported to the `Rpc::` spellings directly** — the historical
   `Mcp::McpTransport` / `Mcp::McpSession` / `Mcp::McpStdioClientTransport` names were
   removed in the #27 refactor with no compatibility aliases; `McpClient` and
   `McpServer` own an `Rpc::JsonRpcSession` and register the MCP notification handlers
   themselves. The `tst_Mcp*` suite stayed green throughout.
4. **`AcpClient` uses `Rpc::JsonRpcSession` directly** — ACP has no progress token and its
   cancellation is `session/cancel` (a session-scoped notification), so it needs none of
   the MCP-specific helpers.

MCP-specific transports — `McpStdioServerTransport` (server stdio) and the Streamable
HTTP transports `McpHttpTransport` / `McpHttpServerTransport` — stay in `LLMQore::Mcp`
and inherit `Rpc::Transport`.

## Stdio framing — reused verbatim

The agent launch + framing already handles the cases ACP needs:

- `QProcess` with separate stdout/stderr channels; **stderr is surfaced** line-by-line
  via `Rpc::StdioClientTransport::stderrLine()` — useful for showing agent diagnostics
  in the host.
- Windows wrapping of `npx`/`npm`/`pnpm`/`yarn`/`uvx` and `.cmd`/`.bat` via
  `cmd.exe /c` — directly relevant since several ACP agents ship as npm shims.
- Graceful stop (`closeWriteChannel` → `waitForFinished` → `kill`).

`AcpAgentConfig` is a thin wrapper over the existing `StdioLaunchConfig`
(`program`, `arguments`, `environment`, `workingDirectory`, startup/stop timeouts),
plus built-in templates for common agents.

## Invariants

- **One dispatcher implementation.** MCP and ACP share `JsonRpcSession`; a bug fixed
  in dispatch/timeout/error handling is fixed for both.
- **MCP regression tests gate the refactor.** The move changes no MCP behavior; the
  existing `tst_Mcp*` suite stays green (it did).
- **Transport reparenting rule is preserved.** Caller owns the transport; sessions and
  clients never reparent it.
