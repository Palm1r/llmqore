# MCP class diagram

```mermaid
classDiagram
    class McpTransport {
        <<abstract>>
        // transport lifecycle + message I/O
    }

    class McpPipeTransport
    class McpStdioClientTransport
    class McpStdioServerTransport
    class McpHttpTransport
    class McpHttpServerTransport

    McpTransport <|-- McpPipeTransport
    McpTransport <|-- McpStdioClientTransport
    McpTransport <|-- McpStdioServerTransport
    McpTransport <|-- McpHttpTransport
    McpTransport <|-- McpHttpServerTransport

    class McpSession {
        // request dispatch + handler registration
        // cancellation + progress reporting
    }

    McpSession --> McpTransport : uses

    class McpClient {
        // handshake + capability negotiation
        // tool, resource, prompt operations
        // provider registration (roots, sampling, elicitation)
        toolsChanged()
        initialized(InitializeResult)
    }

    class McpServer {
        // tool, resource, prompt registration
        // logging + sampling + elicitation
    }

    McpClient --> McpSession : owns
    McpServer --> McpSession : owns
    McpClient --> BaseRootsProvider : optional
    McpClient --> BaseClient : optional (sampling)
    McpClient --> BaseElicitationProvider : optional
    McpServer --> BasePromptProvider : 0..*
    McpServer --> BaseResourceProvider : 0..*
    McpServer --> ToolsManager : optional

    class BaseTool {
        <<abstract>>
        // identity + schema + async execution
    }

    class McpRemoteTool {
        // delegates execution to McpClient
    }

    BaseTool <|-- McpRemoteTool
    McpRemoteTool --> McpClient : uses

    class McpToolBinder {
        // discovers remote tools, registers into ToolsManager
        bound(int)
        failed(QString)
    }

    McpToolBinder --> McpClient : listens to toolsChanged
    McpToolBinder --> ToolsManager : registers McpRemoteTool into

    class BasePromptProvider {
        <<abstract>>
        // list, get, complete prompts
        listChanged()
    }

    class BaseResourceProvider {
        <<abstract>>
        // list, read, complete resources
        listChanged()
        resourceUpdated(uri)
    }

    class BaseRootsProvider {
        <<abstract>>
        // list workspace roots
        listChanged()
    }

    class BaseElicitationProvider {
        <<abstract>>
        // collect structured input from user
    }
```

## Ownership rules

- **`McpSession`** — owned by `McpClient` or `McpServer`. Never outlives owner.
- **`McpTransport`** — passed via constructor, NOT reparented. Caller owns lifetime.
- **`McpRemoteTool`** — parented to `ToolsManager` (via `addTool`). Dies with manager or on `McpToolBinder` refresh.
- **Providers** (`BasePromptProvider`, `BaseResourceProvider`, `BaseRootsProvider`, `BaseElicitationProvider`) and sampling `BaseClient` — held as `QPointer`. Caller owns, must outlive server/client.
- **`BaseTool` via `McpServer::addTool()`** — NOT reparented (unlike `ToolsManager::addTool`).
