# BaseClient contract

Abstract base for every LLM provider client. Owns HTTP transport, request bookkeeping, the per-request message object, the tool-call loop, and signal dispatch. Subclasses supply the wire format, the message translator, and the continuation shape.

---

## Responsibilities

**HTTP transport.** `BaseClient` issues both buffered and streaming HTTP requests through an `HttpTransport`, and manages the lifecycle of each streaming reply, wiring chunk and completion signals into internal handlers. The transport is a constructor argument -- every provider client accepts one after `model` -- and defaults to a privately owned `HttpClient`; a supplied transport stays owned by the caller. Passing a fake transport is how provider clients are tested end to end (see `tests/FakeHttpTransport.hpp`): SSE bytes go in, `chunkReceived` / `requestCompleted` come out, and the continuation request of a tool round-trip can be inspected as it is sent.

**Request bookkeeping.** Every in-flight request is tracked in a central map keyed by a unique request ID. Each entry holds the stream handle, response buffers (line framer and SSE parser), the message object, the original URL and payload (needed for tool continuations), the completed tool-round count, and the captured stop reason. Both the entry type and the map live behind `struct Impl` in `BaseClient.cpp`, not in the public header.

**Message translation.** The per-request message object -- a `BaseMessage` subclass -- is owned by the base, not by the provider. `ensureMessage<T>(id)` is the lazy-create-or-restart skeleton every provider's stream handler runs: it returns the existing object, starting a new continuation round if the previous one ended in `RequiresToolExecution`, and otherwise allocates one. `messageAs<T>(id)` reads it back without creating. The base destroys it when the request ends, so no provider owns message lifetime.

**Tool-call loop.** When a response ends with pending tool calls, `BaseClient` walks the tool-use blocks from the message, dispatches them through `ToolsManager`, collects that round's results, and asks the provider subclass to build a continuation payload. The new payload is re-posted under the same request ID. The round counter lives in the request entry, so it dies with the request; the loop is bounded by `maxToolContinuations()` (default `kDefaultMaxToolRounds`, 10). The whole loop is private -- callers see only `setMaxToolContinuations()` and the read-only `toolRounds(id)`.

**Request headers and authentication.** `BaseClient` builds every outgoing `QNetworkRequest` itself, from two pieces of state the caller can read and replace: a header map and an `AuthScheme`. Provider clients seed both in their constructor -- Claude with `x-api-key` plus `anthropic-version`, the OpenAI-shaped clients with `Authorization: Bearer`, Google with a `key` query parameter -- and the caller overrides whatever it needs. `setHeader` sets one entry, `setHeaders` replaces the whole map, `setAuthScheme` moves the key to a different header or query parameter. An empty API key sends no credential at all.

**Thinking blocks.** `notifyPendingThinkingBlocks(id)` is the only path that emits `thinkingBlockReceived`. It walks the message's current blocks in wire order and announces each unannounced one exactly once, covering both `ThinkingContent` (text plus signature) and `RedactedThinkingContent` (signature only). The "already announced" mark lives on the block itself (`isNotified()` / `markNotified()`), i.e. in the translator -- there is no per-request counter in the client, and nothing resets across continuations because a new round produces new blocks. Providers accumulate reasoning into their message object and call the hook when a block is complete: when answer text starts arriving, and again at the finish reason.

**Token accounting.** Providers never build a `TokenUsage` themselves. They hand a response object to `applyUsage(id, root)`, which reads it through the provider's `UsageSchema` and merges what it found into the turn's snapshot. A counter the object did not mention is left alone, so the second usage report of a turn -- Claude's `message_delta`, every Gemini chunk, OpenAI's final chunk -- cannot zero a field the first one set. `applyUsage` returns without touching anything when the object says nothing about usage at all, which is what keeps `CompletionInfo::usage` a `nullopt` rather than a zeroed struct. The turn's snapshot is folded into the request total at each continuation, and the total is what reaches `requestFinalized`.

**Signal dispatch.** Text deltas, thinking blocks, tool start/result events, final completion, and errors are delivered as Qt signals (`chunkReceived`, `accumulatedReceived`, `thinkingBlockReceived`, `toolStarted`, `toolResultReady`, `requestCompleted`, `requestFinalized`, `requestFailed`). All signals are emitted on the `BaseClient`'s owning thread; Qt's default `AutoConnection` queues cross-thread delivery safely.

---

## Provider subclass contract

### Pure virtual

Public, the caller-facing surface:

- `sendMessage(payload, endpoint, mode)` -- shape the payload for the provider, then hand it to `sendRequest`.
- `ask(prompt, mode)` -- the minimal single-prompt convenience payload.
- `listModels(endpoint)` -- almost always one line over `fetchModelList`.

`url()`, `apiKey()` and `model()` are the only way to read the endpoint triple, from inside the class as well as outside. They were protected fields until the thread guards on the accessors turned out to be decorative: fourteen reads in subclasses went straight past them. Nothing in the tree stores a copy.

Protected, the format-facing surface:

- `toolDialect()` -- the provider's `ToolDialect`, returned from its message translator (`FooMessage::toolDialect()`). This is what `ToolsManager` serializes tool definitions through. It is a method on the client rather than something read off the message object because `tools()` may be called before any request, i.e. before a translator exists.
- `usageSchema()` -- where the provider's token counters live: the name of the container object (empty for counters at the root), and for each of the four counters an optional nesting object plus a field name. A client that reports no usage returns `kNoUsageSchema`. Being pure virtual is the point: a new provider does not compile until it says how it spells usage.
- `processBufferedBody(id, body)` -- a whole non-streamed body, already parsed and already known to carry no error envelope. The base owns that prologue: `processBufferedResponse` parses the bytes, fails the request on invalid JSON, runs `errorMessageFrom` over the object, and only then calls this. A subclass never sees the raw bytes and never re-derives what a broken buffered response looks like.
- `buildContinuationPayload(originalPayload, message, toolResults)` -- the assistant turn plus the tool results, in the provider's wire format. `appendChatContinuation<FooMessage>` is the whole method for any provider whose turns are a plain `messages` array.

### Hooks with a working default

Override only when the provider deviates:

- `processSseEvent(id, event, json)` -- one framed SSE event, already parsed. This is where an SSE provider does its work; `event.type` carries the wire event name for providers that dispatch on it (Responses), and providers that dispatch on the JSON body (Claude, OpenAI, Google) ignore it.
- `streamFraming()` -- how this provider frames its stream: `ServerSentEvents` (the default) or `JsonLines` (Ollama). A request allocates exactly one framer, chosen from this, so `requestSSEParser(id)` and `requestLineFramer(id)` are each valid only under the matching answer and assert otherwise. This is also why `BaseClient.hpp` no longer drags `SSEParser.hpp` and `RpcLineFramer.hpp` into every translation unit that includes a client.
- `processData(id, data)` -- raw streaming bytes. The default feeds `requestSSEParser(id)` and hands whatever it framed to `dispatchSseEvents`. Override only to inspect the bytes first (Google sniffs for a non-SSE error body, then calls `BaseClient::processData`); a different framing is `streamFraming()`, not an override here.
- `errorAnnotations()` -- the provider's error-envelope vocabulary, as data. `errorMessageFrom(body)` reads `error.message` out of a decoded body and appends one parenthesised clause per annotation whose field is present; an annotation with an empty label prints the value bare. An `error` that is a plain string rather than an object (Ollama) is read as the message directly, so Ollama overrides nothing. Both the HTTP path and the buffered path go through this one function, which is why they cannot drift.
- `parseHttpError(response)` -- the whole vendor error envelope, including the `"HTTP <status>: "` prefix and the fallback to a body snippet when there is no envelope at all. The default is built on `errorAnnotations()`; override it only for a provider whose errors are not JSON.
- `flushStreamBuffers(id)` -- called by the base at end of stream *before* it decides the request is done. The default flushes the SSE parser, so every SSE provider gets its trailing partial event for free; Ollama overrides it to drain the line framer.
- `takePendingStreamError(id)` -- an error the provider recognised mid-stream but could not act on yet. The base drains it before concluding the stream succeeded. Google alone uses it, for the 200-with-error-body case its sniffer catches.
- `onStreamDrained(id)` -- the stream is drained and the request is still alive; last chance to finish the message off before the base reads its state. Google dispatches its tool calls here, because its finish reason arrives inside a candidate rather than as an event of its own.
- `cleanupDerivedData(id)` -- per-request state beyond the message object. Only providers that keep survives-the-turn bookkeeping need it (Google's failed-request set and error sniffers, Responses' item-id map).
- `onStreamFinished(id, error)` -- the whole end-of-stream sequence. Nothing in the tree overrides it, and nothing should: the hooks above are the seams cut out of it.

Not a hook, but the same idea: `setLogCategory()` decides which category the shared base code logs under, so a subclass does not report under its parent's name. Call it once, first thing in the constructor. A client that derives from another (Mistral, llama.cpp) must route every one of its constructors through the one that sets it.

### End of stream

One sequence, shared by all seven providers:

1. a transport error, or `takePendingStreamError`, fails the request;
2. `flushStreamBuffers` drains whatever the framer still holds;
3. `onStreamDrained` lets the provider finish the message;
4. a message left in `RequiresToolExecution` hands control to the tool loop and returns;
5. otherwise the stop reason is captured and the request completes.

Each step re-checks that the request is still alive, because any of them can emit a signal whose handler cancels it.

### Shared helpers the subclass calls

- `fetchModelList(url, arrayKey, idKey, idMapper)` and `endpointUrl(endpoint, defaultPath)` -- model listing without a hand-rolled `QFuture`.
- `createRequest`, `sendRequest`, `hasRequest`, `storeRequestContext`.
- `addChunk`, `completeRequest`, `failRequest`, `captureStopReason`.
- `applyUsage` -- the whole of token accounting, see below.
- `executeToolsFromMessage`, `notifyPendingThinkingBlocks`, `cleanupFullRequest`.

### Typical sendMessage pattern

```cpp
RequestID FooClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    const RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/chat") : endpoint;
    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}
```

Nothing else is needed to get bytes flowing: the base frames them and calls the per-event hook, where the `FooMessage` for `id` is allocated on the first event that carries content.

```cpp
void FooClient::processSseEvent(
    const RequestID &id, const SSEEvent &, const QJsonObject &json)
{
    auto *message = ensureMessage<FooMessage>(id);
    ...
}
```

`endpoint` lets the caller pick a non-default path on providers that expose more than one (e.g. Mistral's `/fim/completions`). Passing an empty string selects the provider default, so single-endpoint clients can ignore the argument beyond the empty-check shown above.

Consumers subscribe to `BaseClient` signals (`chunkReceived`, `requestCompleted`, `requestFinalized`, `requestFailed`, `toolStarted`, `toolResultReady`, `thinkingBlockReceived`) to observe request progress. All signals are emitted on the `BaseClient`'s owning thread; Qt's `AutoConnection` handles cross-thread queued delivery.

A host that keeps its own conversation history must carry `CompletionInfo::requestPayload` forward rather than its original request: that field is the payload of the last turn actually sent, tool round-trips included.

---

## Error handling

Errors reach the caller through three paths:

- **Transport errors** -- DNS failures, timeouts, SSL errors, aborted connections, and connection refused. These originate from the network layer and are forwarded as failure notifications.
- **HTTP errors (4xx/5xx)** -- When non-success status headers arrive, the client switches to error mode and accumulates the response body. At stream end, `parseHttpError` gets a chance to read the vendor-specific error format. If it declines, a default message with the status code and a body snippet is used.
- **Parse/protocol errors** -- Malformed streams or unexpected JSON structures detected during provider-specific parsing. The provider reports these directly as request failures.

---

## Checklist: adding a new provider

1. Create a new folder under `source/clients/` with the client and message translator files.
2. Add a public header under `include/LLMQore/`, and list it in the `include/LLMQore/Clients` umbrella header.
3. In the translator's `.cpp`, define the provider's `ToolDialect` subclass (anonymous namespace) and expose it through a static `FooMessage::toolDialect()`. Both directions of the format -- schema out, tool results back -- belong in this one file.
4. Implement the pure virtuals: `sendMessage`, `ask`, `listModels`, `toolDialect`, `usageSchema`, `processBufferedBody`, `buildContinuationPayload`. The usage schema is a `constexpr UsageSchema` next to the client, like the dialect is next to the translator. Seed the default `AuthScheme` and header map in the constructor -- there is no request-building hook to override.
5. Override `processSseEvent()` for the streaming path, and call `setLogCategory()` in the constructor so the shared base code logs under the new provider's name. A provider that is not SSE-framed overrides `processData()` and `flushStreamBuffers()` instead.
6. Use `ensureMessage<FooMessage>(id)` in the stream handler; do not keep a message map in the client.
7. Add unit tests: constructor sanity and header shape (`tst_RequestHeaders` is parameterised over every provider), the tool schema shape in `tst_ToolsManager`, model listing over `FakeHttpTransport` in `ListModels`, error rendering in `ParseHttpError`, and translator behaviour in a `tst_FooMessage` suite that needs no event loop.
