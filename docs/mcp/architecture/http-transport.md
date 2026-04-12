# `McpHttpServerTransport`

Server-side HTTP transport. Speaks 2025-03-26 Streamable HTTP on a `QTcpServer`-based HTTP/1.1 parser — no dependency on `Qt6::HttpServer`.

## Behaviour

- **Loopback by default.** `HttpServerConfig::address` = `LocalHost`, `port = 0` (ephemeral). Read via `serverPort()`.
- **Single path, single session.** Fixed `config.path` (default `/mcp`); anything else → 404. Non-POST → 405. Single random `Mcp-Session-Id` generated at `start()`; mismatched inbound session ids → 400.
- **Origin guard.** `HttpServerConfig::allowedOrigins` non-empty → POSTs without matching `Origin` → 403. Empty list is permissive (local-dev only).
- **Request → response routing.** Tracks `requestId → QTcpSocket`. `McpSession` calls `send()` with matching response id → single `application/json` body on that socket.
- **Server-initiated messages piggybacked.** `send()` with no matching pending id (notifications, `createSamplingMessage`, `createElicitation`) queued → flushed onto next POST response as `text/event-stream` body. No long-lived `GET /mcp` push channel (out of scope).

## Smoke test

`tst_McpHttpServer.HandshakeAndToolCallOverHttp` — pairs `McpHttpTransport` (client) against `McpHttpServerTransport` (server) over TCP: handshake + `tools/list` + `tools/call` round-trip.
