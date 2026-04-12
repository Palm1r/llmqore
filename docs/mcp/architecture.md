# MCP architecture

Two parallel stacks meeting in `ToolsManager`:

1. **LLM provider stack** — `BaseClient` + per-provider subclass, `BaseMessage` parser, `HttpClient`/SSE, `ToolsManager`.
2. **MCP stack** — `McpTransport` + `McpSession` + `McpClient`/`McpServer`, provider abstractions (`BasePromptProvider`, `BaseResourceProvider`, `BaseRootsProvider`, `BaseElicitationProvider`), bridge classes (`McpRemoteTool`, `McpToolBinder`). Sampling uses `BaseClient` directly via `setSamplingClient()` — no separate provider abstraction.

Stacks are independent: use as LLM client without MCP, or run `McpServer` without `BaseClient`. `McpToolBinder` is the glue registering remote tools into `ToolsManager`.

| Topic | Doc |
|---|---|
| Class diagram + ownership | [`architecture/classes.md`](architecture/classes.md) |
| End-to-end request flow | [`architecture/request-flow.md`](architecture/request-flow.md) |
| Async chain model | [`architecture/async-patterns.md`](architecture/async-patterns.md) |
| Content type hierarchies | [`architecture/content-types.md`](architecture/content-types.md) |
| HTTP hosting internals | [`architecture/http-transport.md`](architecture/http-transport.md) |
| Sampling (`createMessage`) | [`architecture/sampling.md`](architecture/sampling.md) |
| Elicitation (`elicitation/create`) | [`architecture/elicitation.md`](architecture/elicitation.md) |
| Exception hierarchy | [`architecture/exceptions.md`](architecture/exceptions.md) |

---

## Layered architecture

```mermaid
flowchart TD
    subgraph User["Host application"]
        APP["Your code"]
    end

    subgraph Public["Public API (provider clients)"]
        CC["ClaudeClient"]
        OC["OpenAIClient"]
        OR["OpenAIResponsesClient"]
        GC["GoogleAIClient"]
        OL["OllamaClient"]
        LC["LlamaCppClient"]
    end

    subgraph Core["Core infrastructure"]
        BC["BaseClient<br/><small>request lifecycle, callbacks</small>"]
        TM["ToolsManager<br/><small>tool registry + exec queue</small>"]
        BM["BaseMessage<br/><small>streaming parser per provider</small>"]
        BT["BaseTool<br/><small>user-defined tool interface</small>"]
    end

    subgraph Network["Network layer"]
        HC["HttpClient<br/><small>QNetworkAccessManager wrapper</small>"]
        SSE["SSEBuffer + SSEUtils<br/><small>text/event-stream framing</small>"]
    end

    subgraph MCP["MCP stack"]
        MCPS["McpSession<br/><small>JSON-RPC dispatcher</small>"]
        MC["McpClient"]
        MS["McpServer"]
        MT["McpTransport<br/><small>(abstract)</small>"]
        MRT["McpRemoteTool"]
        MTB["McpToolBinder"]
        BPP["BasePromptProvider"]
        BRP["BaseResourceProvider"]
        BRootP["BaseRootsProvider"]
        BEP["BaseElicitationProvider"]
    end

    subgraph Transports["Transport implementations"]
        Pipe["McpPipeTransport<br/><small>in-process pair, tests</small>"]
        StdioC["McpStdioClientTransport<br/><small>launches child process</small>"]
        StdioS["McpStdioServerTransport<br/><small>reads stdin/stdout</small>"]
        Http["McpHttpTransport<br/><small>client, 2024-11-05 + 2025-03-26</small>"]
        HttpS["McpHttpServerTransport<br/><small>server, QTcpServer + HTTP/1.1</small>"]
    end

    APP --> Public
    Public --> BC
    BC --> TM
    BC --> BM
    BC --> HC
    HC --> SSE
    TM --> BT

    APP -.optional.-> MCP
    MCP -.registers tools into.-> TM
    MTB --> MC
    MTB --> TM
    MTB --> MRT
    MRT --> MC
    MRT -.is a.-> BT
    MC --> MCPS
    MS --> MCPS
    MS --> BPP
    MS --> BRP
    MC --> BRootP
    MC --> BEP
    MCPS --> MT
    MT --> Pipe
    MT --> StdioC
    MT --> StdioS
    MT --> Http
    MT --> HttpS
```

## Invariants

- **Provider clients never talk to `McpSession` directly.** MCP tools are `McpRemoteTool` instances in `ToolsManager`.
- **`McpTransport` is the only byte-level boundary.** Rest works on `QJsonObject`.
- **`BaseClient` owns HTTP side; `McpSession` owns JSON-RPC side.** No shared code/types.
- **One `ToolsManager` holds tools from multiple sources** — indistinguishable to `buildContinuationPayload`.
