# Changelog

While LLMQore is below 1.0, every minor release may break source and binary compatibility. The CMake package accepts only the same `major.minor` (`SameMinorVersion`), and the shared library's SOVERSION is `major.minor`, so an application built against 0.8 never loads 0.9 by accident. Patch releases within a minor stay compatible.

## 0.9.0 (unreleased)

### Breaking

Rebuild against 0.9 and follow [Migrating to 0.9](docs/migration-0.9.md). In short:

- `Mcp::McpHttpTransport` is now `Mcp::McpStreamableHttpTransport` (2025-03-26 and later) and `Mcp::McpSseHttpTransport` (2024-11-05); `Mcp::makeHttpTransport()` picks one from `HttpTransportConfig::spec`.
- `MistralClient` is gone; use `OpenAIClient` with `setProfile(mistralProfile())`.
- Subclasses of `BaseClient` and `BaseMessage` move to a new extension surface: `processBufferedBody()`, `streamFraming()` / `processJsonLine()`, `errorAnnotations()`, `recordStopReason()`, `clearDerivedCaches()`.
- `Rpc::Transport` gains the virtual `abandon(requestId)`, and the layout of `HttpTransportConfig`, `SSEParser`, `McpServer`, `BaseClient` and `BaseMessage` changed.

### Changed

- `BaseClient::ask()` attaches the enabled tools from its `ToolsManager` when the registry is not empty; `ask(QString)` goes through a one-turn `Conversation`.
- Provider errors are parsed one way for every client, and an error inside a stream or a 2xx body fails the request.
- Text written before a tool call stays in the answer.
- A new `initialize` over Streamable HTTP starts a new session.
- `SSEEvent::id` carries over between events, as WHATWG specifies.
- `McpToolBinder` does not ask `tools/list` of a server without the tools capability.
- A request that times out sends `notifications/cancelled` (never for `initialize`).

### Added

- Streamable HTTP: a listen stream for server-initiated messages, with `Last-Event-ID` resumption and the server's `retry`.
- Streamable HTTP: a POST stream that drops before its answer is resumed with `GET` and `Last-Event-ID`.
- Streamable HTTP: the client ends its session with `DELETE` on `stop()`, on a new `initialize` and on destruction.
- `McpHttpServerTransport` accepts `DELETE` and answers 404 for an ended session.
- Cancelling or timing out a request frees its HTTP connection (`Rpc::Transport::abandon`).
- `Rpc::Transport::sendFailed`, so a request whose message never left fails at once.
- `CallbackElicitationProvider`, `DirectoryRootsProvider`, `StaticResourceProvider`.
- `ProviderProfile` and a `*Profile()` function for every bundled client.

### Fixed

- Protocol headers (`Accept`, `Content-Type`, `Mcp-Session-Id`, `MCP-Protocol-Version`, `Last-Event-ID`, `Cache-Control`) can no longer be overridden from `HttpTransportConfig::headers`; such entries are dropped with one warning.
- The 2024-11-05 transport survives a handler that stops, restarts or deletes it from inside one of its signals.
- Debug builds assert that a provider handed to `McpClient` or `McpServer` lives on its thread.
