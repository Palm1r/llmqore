# ACP host class diagram

```mermaid
classDiagram
    class Rpc_Transport {
        <<abstract>>
        // transport lifecycle + message I/O
        // start() stop() isOpen() send(QJsonObject)
        // messageReceived() closed() stateChanged()
    }

    class StdioClientTransport {
        // launches agent via QProcess
        // newline-delimited JSON framing
        // stderrLine() for agent diagnostics
    }

    class PipeTransport {
        // in-process pair, used by loopback tests
    }

    Rpc_Transport <|-- StdioClientTransport
    Rpc_Transport <|-- PipeTransport

    class Rpc_JsonRpcSession {
        // request dispatch + handler registration
        // pending table + timeouts + error replies
        sendRequest(method, params) QFuture
        sendNotification(method, params)
        setRequestHandler(method, fn)
        setNotificationHandler(method, fn)
    }

    Rpc_JsonRpcSession --> Rpc_Transport : uses

    class AcpClient {
        // handshake + capability negotiation
        // session multiplexing (N sessions / 1 agent)
        // outgoing: initialize, newSession, prompt, cancel
        // incoming: routes update + agent->host requests
        connectAndInitialize() QFuture
        newSession(cwd, mcpServers) QFuture
        prompt(sessionId, blocks) QFuture
        cancel(sessionId)
        agentMessageChunk(sessionId, text)
        toolCallUpdated(sessionId, ToolCall)
        planUpdated(sessionId, Plan)
        promptFinished(sessionId, stopReason)
    }

    AcpClient --> Rpc_JsonRpcSession : owns
    AcpClient --> AcpAgentConfig : configured by

    class AcpAgentConfig {
        // program, arguments, env, cwd
        // built-in templates (claude-code, gemini-cli, codex)
    }

    class AcpPermissionProvider {
        <<abstract>>
        // ask user to allow/reject a tool call
        requestPermission(sessionId, toolCall, options) QFuture
    }

    class AcpFileSystemProvider {
        <<abstract>>
        // serve fs/read_text_file + fs/write_text_file
        readTextFile(sessionId, path, line, limit) QFuture
        writeTextFile(sessionId, path, content) QFuture
    }

    class AcpTerminalProvider {
        <<abstract>>
        // serve terminal/* on behalf of the agent
        create(sessionId, command, args, cwd, env) QFuture
        output(sessionId, terminalId) QFuture
        waitForExit(sessionId, terminalId) QFuture
        kill(sessionId, terminalId)
        release(sessionId, terminalId)
    }

    AcpClient --> AcpPermissionProvider : optional (advertises capability)
    AcpClient --> AcpFileSystemProvider : optional (advertises fs.*)
    AcpClient --> AcpTerminalProvider : optional (advertises terminal)

    class TerminalManager {
        // default AcpTerminalProvider
        // QProcess per terminalId, ring-buffered output
    }

    AcpTerminalProvider <|-- TerminalManager
    class DefaultFileSystemProvider {
        // QFile-backed default
    }
    AcpFileSystemProvider <|-- DefaultFileSystemProvider
```

## Ownership rules

- **`Rpc::JsonRpcSession`** — owned by `AcpClient`. Never outlives its owner.
- **`Rpc::Transport`** — passed via constructor, NOT reparented. Caller owns lifetime
  (same rule as `McpTransport` today). For stdio, `AcpClient::shutdown()` stops the
  transport, which terminates the agent child process gracefully then `kill()`s.
- **Providers** (`AcpPermissionProvider`, `AcpFileSystemProvider`,
  `AcpTerminalProvider`) — held by `AcpClient` as `QPointer`. Caller owns; they must
  outlive the client. Absence of a provider ⇒ the matching capability is advertised
  `false` in `initialize`.
- **`TerminalManager`'s child processes** — owned by the manager, keyed by
  `terminalId`. Released on `terminal/release`, on session end, or on manager
  destruction (each `QProcess` is killed).
- **Session state** — `AcpClient` holds a `QHash<QString sessionId, SessionState>`.
  A `SessionState` tracks the outstanding `prompt` promise and the live tool calls so
  `tool_call_update` notifications can be merged into the right `ToolCall`.

## Why no `BaseClient` / `ToolsManager` here

In the MCP stack, `McpClient` exists to **bind remote tools into a local model loop**
(`McpToolBinder` → `ToolsManager` → `BaseClient`). The ACP host inverts that: the
remote process *is* the model loop. So none of `BaseClient`, `ToolsManager`,
`BaseMessage`, `McpRemoteTool`, or `McpToolBinder` appear on this path. The only
shared dependency is the JSON-RPC transport/session layer.
