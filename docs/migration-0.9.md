# Migrating to 0.9

0.9 breaks source and binary compatibility with 0.8. Rebuild everything that links LLMQore; `find_package(LLMQore 0.8)` will not accept 0.9. This page lists what you have to change, in the order you are likely to hit it.

## MCP over HTTP

`Mcp::McpHttpTransport` is split by wire revision:

| 0.8 | 0.9 |
|---|---|
| `McpHttpTransport` with `spec = V2025_03_26` | `McpStreamableHttpTransport` |
| `McpHttpTransport` with `spec = V2024_11_05` | `McpSseHttpTransport` |
| either, chosen at run time | `makeHttpTransport(config, transport, parent)` |

```cpp
// 0.8
auto *transport = new Mcp::McpHttpTransport(config);

// 0.9 — the factory reads config.spec
Rpc::Transport *transport = Mcp::makeHttpTransport(config);
```

Both classes live in `<LLMQore/McpHttpTransport.hpp>`, take the same `HttpTransportConfig`, and are plain `Rpc::Transport`s. Behaviour you may notice:

- A second `initialize` starts a new session. Calls still open in the old one fail, and the old session is ended with an HTTP `DELETE`.
- `stop()` and the destructor end the session with `DELETE` as well. A server that answers 405 is fine.
- Once `initialize` returns, the transport keeps a `GET` stream open for messages the server sends on its own. It holds one HTTP connection for the whole session.
- `HttpTransportConfig::headers` can no longer override `Accept`, `Content-Type`, `Mcp-Session-Id`, `MCP-Protocol-Version`, `Last-Event-ID` or `Cache-Control`. Such entries are dropped, and the transport logs one warning when it is created. Put credentials in `Authorization` or your own `X-` headers as before.

## Your own `Rpc::Transport`

`Rpc::Transport` has two new members:

- `sendFailed(message, reason)` — emit it when a request you were handed can never be answered (the connection refused it, the HTTP request failed). `JsonRpcSession` fails that request at once instead of waiting for its timeout.
- `virtual void abandon(const QString &requestId)` — the session calls it when it stops waiting for a request, after a cancel or a timeout. The default does nothing. Override it if each request holds a resource of its own, such as a connection, and release that resource without emitting `sendFailed`.

## Mistral

`MistralClient` is removed. Mistral speaks the OpenAI chat API, so use `OpenAIClient` with the Mistral profile:

```cpp
// 0.8
auto *client = new LLMQore::MistralClient(url, apiKey, model);

// 0.9
auto *client = new LLMQore::OpenAIClient(url, apiKey, model);
client->setProfile(LLMQore::mistralProfile());
```

The profile keeps the `/v1/chat/completions` and `/v1/models` paths and the `llmqore.mistral` log category. Other endpoints, such as FIM, are still passed to `sendMessage()` as a path.

`setProfile()` replaces the whole profile, including headers set earlier with `setHeader()`, so call it first.

## `ask()` attaches tools

`BaseClient::ask(Conversation, ...)` now adds a `tools` array built from the client's `ToolsManager` whenever it has enabled tools; `extra` is applied after it and wins. `ask(QString)` builds a one-turn `Conversation` and goes the same way. If a model rejects `tools`, pass `{"tools": ...}` yourself in `extra`, disable the tools, or call `sendMessage()`, which adds nothing.

## Subclassing `BaseClient`

The protected surface was rebuilt around a shared request pipeline. A subclass now describes its provider rather than driving the stream:

| 0.8 | 0.9 |
|---|---|
| override `processBufferedResponse(id, QByteArray)` | override `processBufferedBody(id, QJsonObject)`; the base has parsed the JSON and failed the request on an `error` field already |
| override `onStreamFinished()` / `flushStreamBuffers()` | gone; return `StreamFraming::JsonLines` from `streamFraming()` and override `processJsonLine()`, or keep `processSseEvent()` for SSE |
| override `parseHttpError()` | override `errorAnnotations()`; `errorMessageFrom()` and `describeError()` format the message |
| `setLogCategory()`, auth scheme and headers set one by one | `setProfile(ProviderProfile{...})` |
| `messageAs<T>()` | `messageForRequest()` |
| `m_url`, `m_apiKey`, `m_model` | `url()`, `apiKey()`, `model()` |
| post the request yourself | `postJson(payload, endpoint, mode)` |
| set message state with flags | `applyEffects(id, MessageEffects{...})` |

`ask(QString)` is no longer virtual; override `buildConversationPayload()` to shape what `ask()` sends.

## Subclassing `BaseMessage`

| 0.8 | 0.9 |
|---|---|
| override `stopReason()` | call `recordStopReason(reason, StopReasonMap{...})`; `stopReason()` reads it |
| override `startNewContinuation()` | override `clearDerivedCaches()` |
| `getOrCreateTextContentIndex()` | `ensureTextContentIndex()` |

`ToolCallAccumulator`, `completeToolCall()` and `completeAllToolCalls()` collect streamed tool arguments, so a message no longer needs its own buffers for them.

## Headers you included by accident

`BaseClient.hpp` and the client headers no longer include `SSEParser.hpp` or `RpcLineFramer.hpp`. `SSEEvent` moved to `<LLMQore/SSEEvent.hpp>`. Include what you use.

## Providers and threads

A provider handed to `McpClient` or `McpServer` must live on its thread; Debug builds now assert this. See [Thread contract](threading.md).
