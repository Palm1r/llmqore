# MCP exception hierarchy

All MCP errors propagate as `McpException` subclasses. Inherits `QException` — flows through `QFuture`/`.onFailed()` with full type preservation.

```mermaid
classDiagram
    class QException {
        <<Qt base>>
    }

    class McpException {
        // error message
    }

    class McpTransportError
    class McpProtocolError
    class McpTimeoutError
    class McpCancelledError
    class McpRemoteError {
        // numeric code + remote message + data payload
    }

    QException <|-- McpException
    McpException <|-- McpTransportError
    McpException <|-- McpProtocolError
    McpException <|-- McpTimeoutError
    McpException <|-- McpCancelledError
    McpException <|-- McpRemoteError
```

## Meanings

| Exception | When |
|---|---|
| `McpTransportError` | Transport not open, network down, subprocess died, SSE stream broke |
| `McpProtocolError` | Invalid JSON-RPC envelope, unknown method, invalid state |
| `McpTimeoutError` | Request exceeded `sendRequest` timeout |
| `McpCancelledError` | Peer sent `notifications/cancelled`, or `cancelRequest` called |
| `McpRemoteError` | Peer replied with JSON-RPC `error` object. Carries numeric `code`, remote `message`, `data` payload. Typically the one to catch separately in user code |

Every subtype implements `raise()` + `clone()` correctly — `.onFailed(ctx, [](const McpRemoteError &e) {...})` matches the concrete subtype without slicing.
