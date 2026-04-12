# Sampling architecture (`sampling/createMessage`)

Reverse of tool-calling: MCP **server** hands **client** a prompt → client runs it through `BaseClient` → returns completion.

## Setup

Register a sampling client and payload builder on McpClient. This enables the `sampling` capability. Without one, sampling requests receive `MethodNotFound`.

## Flow

1. Server sends `sampling/createMessage`.
2. McpClient parses params, runs host builder to produce a provider-specific payload.
3. Sends the payload through the sampling BaseClient -- completion resolves a promise, failure rejects with McpRemoteError.
4. BaseClient streams normally. Tools execute via ToolsManager (local + MCP tools indistinguishable), up to 10 continuations.
5. Final turn produces `{fullText, model, stopReason}`.
6. Result is wrapped into CreateMessageResult and returned as a JSON-RPC response to the server.

## Tool-calling inside sampling

Comes for free. Whatever tools are on `BaseClient`'s `ToolsManager` (local + remote via `McpToolBinder`) work unchanged. Sampling IS a `sendMessage` with tool loop.

## Server side

McpServer short-circuits with McpProtocolError when not initialized or client lacks `sampling` capability. Otherwise sends a normal session request.

## Flow over HTTP

Sampling requests from the server are buffered when no matching client socket exists. The next client POST picks them up as an SSE event. The client drives BaseClient, then POSTs the result back as a new request, which is routed to the pending promise.

## Stop reason passthrough

Provider stop reasons forwarded **raw** — Claude `"end_turn"/"tool_use"/...`, OpenAI `"stop"/"tool_calls"/...`, Google `"STOP"/...`, Ollama arbitrary strings. MCP `CreateMessageResult.stopReason` accepts custom strings — passthrough is spec-compliant.

## Wire types and tests

`SamplingMessage`, `ModelHint`, `ModelPreferences`, `CreateMessageParams`, `CreateMessageResult` in `McpTypes.hpp`. Round-trip: `tst_McpTypes.cpp`. E2E: three `McpLoopbackTest.Sampling*` cases.
