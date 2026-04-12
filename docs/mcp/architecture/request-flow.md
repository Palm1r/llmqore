# End-to-end request flow

User prompt → model calls remote MCP tool → conversation continues.

```mermaid
sequenceDiagram
    participant App as Host app
    participant CC as ClaudeClient
    participant BC as BaseClient
    participant HC as HttpClient
    participant TM as ToolsManager
    participant MRT as McpRemoteTool
    participant MC as McpClient
    participant MCS as McpSession (client)
    participant MT as McpTransport (client)
    participant MT2 as McpTransport (server)
    participant MSS as McpSession (server)
    participant MS as McpServer
    participant Tool as BaseTool (server)

    Note over App,MC: Setup (once, at startup)
    App->>MC: connect and initialize
    MC->>MCS: request "initialize"
    MCS->>MT: send
    MT->>MT2: (stdio / pipe / http)
    MT2->>MSS: receive message
    MSS->>MS: dispatch "initialize"
    MS-->>MSS: capabilities result
    MSS->>MT2: respond
    MT2->>MT: (wire)
    MT->>MCS: receive message
    MCS-->>MC: resolve with capabilities
    App->>App: bind remote tools
    Note over App,TM: Binder discovers remote tools, wraps as McpRemoteTool, registers into ToolsManager

    Note over App,Tool: User sends prompt
    App->>CC: send message
    CC->>BC: store request context
    BC->>HC: start streaming POST
    HC->>HC: SSE stream begins

    loop SSE events
        HC-->>BC: raw bytes
        BC->>CC: process data
        CC->>CC: parse SSE into content blocks
        alt text delta
            CC-->>App: text chunk
        else tool use
            CC->>CC: mark as requires tool execution
        end
    end

    HC-->>BC: stream finished
    BC->>BC: detect pending tool calls
    BC->>TM: execute tool call
    TM->>MRT: execute async
    MRT->>MC: call remote tool
    MC->>MCS: request "tools/call"
    MCS->>MT: send
    MT->>MT2: (wire)
    MT2->>MSS: receive message
    MSS->>MS: dispatch "tools/call"
    MS->>Tool: execute async
    Tool-->>MS: ToolResult (text / image / ...)
    MS-->>MSS: resolve with result
    MSS->>MT2: respond
    MT2->>MT: (wire)
    MT->>MCS: receive message
    MCS-->>MC: resolve ToolResult
    MC-->>MRT: resolve ToolResult
    MRT-->>TM: future resolves
    TM-->>BC: tool execution complete

    BC->>BC: build continuation payload
    BC->>CC: serialize tool results into next turn
    Note right of CC: ToolResult serialised as tool_result content block (image blocks preserved)
    BC->>HC: start streaming POST (continuation)

    loop Second SSE stream
        HC-->>CC: text delta events
        CC-->>App: text chunk (final answer)
    end

    HC-->>BC: stream finished
    BC-->>App: request completed
```

## Invariants

- Provider client never learns the tool came from MCP — `McpRemoteTool *` occupies the same `BaseTool *` slot.
- Second HTTP request is a new streaming call from `buildContinuationPayload`. Up to 10 continuations per prompt.
- Server-side `BaseTool` sees the same `QFuture<ToolResult>` contract as a local tool.
