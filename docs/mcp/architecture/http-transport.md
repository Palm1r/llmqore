# `McpHttpServerTransport`

Server-side HTTP transport. Speaks 2025-03-26 Streamable HTTP on a `QTcpServer`-based HTTP/1.1 parser — no dependency on `Qt6::HttpServer`.

## Behaviour

- **Loopback by default.** `HttpServerConfig::address` = `LocalHost`, `port = 0` (ephemeral). Read via `serverPort()`.
- **Single path, single session.** Fixed `config.path` (default `/mcp`); anything else → 404. Methods other than POST and DELETE → 405 with `Allow: POST, DELETE`. One random `Mcp-Session-Id` at a time, generated at construction; mismatched inbound session ids → 400.
- **Session termination.** `DELETE` carrying the current `Mcp-Session-Id` → 200: the session ends, its pending requests are dropped with their sockets, queued server messages are discarded, and a fresh id is generated for whoever initializes next. Any later request carrying an ended id → 404, as the spec requires. `DELETE` without an id → 400. `McpServer` is not told; the transport keeps listening.
- **Origin guard.** `HttpServerConfig::allowedOrigins` non-empty → POSTs and DELETEs without matching `Origin` → 403. Empty list is permissive (local-dev only).
- **Request → response routing.** Tracks `requestId → QTcpSocket`. `JsonRpcSession` calls `send()` with matching response id → single `application/json` body on that socket.
- **Server-initiated messages piggybacked.** `send()` with no matching pending id (notifications, `createSamplingMessage`, `createElicitation`) queued → flushed onto next POST response as `text/event-stream` body. No long-lived `GET /mcp` push channel (out of scope).

## Smoke test

`tst_McpHttpServer.HandshakeAndToolCallOverHttp` — pairs `McpStreamableHttpTransport` (client) against `McpHttpServerTransport` (server) over TCP: handshake + `tools/list` + `tools/call` round-trip.
