# LLMQore architecture

Two parallel stacks that meet in `ToolRegistry` / `ToolsManager`:

1. **LLM provider stack** — `BaseClient` subclass per provider
   (Claude, OpenAI, OpenAI Responses, Google Gemini, Ollama, llama.cpp),
   `BaseMessage` streaming parser, `HttpClient` + `SSEParser`, `ToolsManager`.
2. **MCP stack** — `McpTransport` + `McpSession` + `McpClient` /
   `McpServer`, provider abstractions, `McpToolBinder` glue that exposes
   remote MCP tools as local `BaseTool` instances.

An `McpRemoteTool` is a `BaseTool` subclass — any `BaseClient` can call
MCP tools without knowing they're remote.

```mermaid
flowchart TD
    APP["Host app"]
    subgraph Core["Core (LLM side)"]
        BC["BaseClient<br/>+ per-provider subclass"]
        BM["BaseMessage<br/>+ ContentBlock family"]
        TM["ToolsManager<br/>+ BaseTool"]
        NET["HttpClient<br/>HttpStream · SSEParser"]
    end
    subgraph Mcp["MCP"]
        MC["McpClient / McpServer"]
        MS["McpSession"]
        MT["McpTransport<br/>stdio · pipe · http"]
        MTB["McpToolBinder<br/>McpRemoteTool"]
    end

    APP --> BC
    BC --> BM
    BC --> TM
    BC --> NET
    TM -.holds.-> MTB
    MTB --> MC
    MC --> MS
    MS --> MT
    APP -.optional.-> MC
```

---

## Index

### Core (LLM provider stack)

| Doc | When to read |
|---|---|
| [`architecture/overview.md`](architecture/overview.md) | Full layered view, invariants, source layout. |
| [`architecture/request-lifecycle.md`](architecture/request-lifecycle.md) | `sendMessage` → SSE → tool execution → `requestCompleted`/`requestFinalized`. |
| [`architecture/clients/base-client.md`](architecture/clients/base-client.md) | Adding a provider. Protected virtual contract, `ActiveRequest`, error handling. |
| [`architecture/clients/providers.md`](architecture/clients/providers.md) | Per-provider parser / payload shape cheatsheet. |
| [`architecture/messages-and-content.md`](architecture/messages-and-content.md) | `BaseMessage`, `MessageState`, `ContentBlock` hierarchy. |
| [`architecture/tools.md`](architecture/tools.md) | `BaseTool` / `ToolRegistry` / `ToolsManager` / `ToolResult`, execution queue, rich content in continuations. |
| [`architecture/networking.md`](architecture/networking.md) | `HttpClient`, `HttpStream`, `SSEParser`, `LineBuffer`, error taxonomy. |

### MCP stack

| Doc | When to read |
|---|---|
| [`mcp/architecture.md`](mcp/architecture.md) | MCP-stack overview and per-subsystem index. |
| [`mcp/architecture/classes.md`](mcp/architecture/classes.md) | MCP class diagram + ownership rules. |
| [`mcp/architecture/request-flow.md`](mcp/architecture/request-flow.md) | End-to-end sequence: prompt → remote MCP tool call. |
| [`mcp/architecture/async-patterns.md`](mcp/architecture/async-patterns.md) | `.then()` / `.onFailed()` idioms. |
| [`mcp/architecture/content-types.md`](mcp/architecture/content-types.md) | `ContentBlock` vs `ToolResult` in continuation payloads. |
| [`mcp/architecture/http-transport.md`](mcp/architecture/http-transport.md) | `McpHttpServerTransport` internals, piggyback buffer. |
| [`mcp/architecture/sampling.md`](mcp/architecture/sampling.md) | `setSamplingClient`, `requestFinalized` plumbing. |
| [`mcp/architecture/elicitation.md`](mcp/architecture/elicitation.md) | `elicitation/create` flow. |
| [`mcp/architecture/exceptions.md`](mcp/architecture/exceptions.md) | `McpException` hierarchy. |

---

## Directory layout

```
docs/
├── architecture.md               ← you are here
├── architecture/                 ← core (LLM-side) architecture notes
│   ├── overview.md
│   ├── request-lifecycle.md
│   ├── clients/
│   │   ├── base-client.md
│   │   └── providers.md
│   ├── messages-and-content.md
│   ├── tools.md
│   └── networking.md
├── quick-start.md                ← usage walk-through
├── integration.md                ← CMake integration
└── mcp/
    ├── architecture.md           ← MCP-specific architecture entry
    ├── architecture/             ← MCP-specific architecture notes
    └── mcp_protocol_coverage.md
```

---

## Starting points by task

- **Add a new LLM provider** → [`architecture/clients/base-client.md`](architecture/clients/base-client.md), then [`providers.md`](architecture/clients/providers.md).
- **Add a tool that returns images** → [`architecture/tools.md`](architecture/tools.md).
- **Requests hanging / SSE issues** → [`architecture/networking.md`](architecture/networking.md) + [`architecture/request-lifecycle.md`](architecture/request-lifecycle.md).
- **Add a new content block type** → [`architecture/messages-and-content.md`](architecture/messages-and-content.md) + [`mcp/architecture/content-types.md`](mcp/architecture/content-types.md).
- **MCP internals** → [`mcp/architecture.md`](mcp/architecture.md).
