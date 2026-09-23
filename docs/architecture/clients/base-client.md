# BaseClient contract

Abstract base for every LLM provider client. Owns HTTP transport, request bookkeeping, the per-request message object, the tool-call loop, and signal dispatch. Subclasses supply the wire format, the message translator, and the continuation shape.

---

## Responsibilities

**HTTP transport.** `BaseClient` issues both buffered and streaming HTTP requests through an `HttpTransport`, and manages the lifecycle of each streaming reply, wiring chunk and completion signals into internal handlers. The transport is a constructor argument -- every provider client accepts one after `model` -- and defaults to a privately owned `HttpClient`; a supplied transport stays owned by the caller. Passing a fake transport is how provider clients are tested end to end (see `tests/FakeHttpTransport.hpp`): SSE bytes go in, `chunkReceived` / `requestCompleted` come out, and the continuation request of a tool round-trip can be inspected as it is sent.

**Request bookkeeping.** Every in-flight request is tracked in a central map keyed by a unique request ID. Each entry holds the stream handle, response buffers (the one framer `streamFraming()` asked for, and the answer text accumulated across every round of the request), the message object, the original URL and payload (needed for tool continuations), the completed tool-round count, and the captured stop reason. Both the entry type and the map live behind `struct Impl` in `BaseClient.cpp`, not in the public header.

**Message translation.** The per-request message object -- a `BaseMessage` subclass -- is owned by the base, not by the provider. `ensureMessage<T>(id)` is the lazy-create-or-restart skeleton every provider's stream handler runs: it returns the existing object, starting a new continuation round if the previous one ended in `RequiresToolExecution`, and otherwise allocates one. `messageForRequest(id)` reads it back without creating or restarting anything. The base destroys it when the request ends, so no provider owns message lifetime.

**Tool-call loop.** When a response ends with pending tool calls, `BaseClient` walks the tool-use blocks from the message, dispatches them through `ToolsManager`, collects that round's results, and asks the provider subclass to build a continuation payload. The new payload is re-posted under the same request ID. The round counter lives in the request entry, so it dies with the request; the loop is bounded by `maxToolContinuations()` (default `kDefaultMaxToolRounds`, 10). The whole loop is private -- callers see only `setMaxToolContinuations()` and the read-only `toolRounds(id)`.

**Request headers and authentication.** `BaseClient` builds every outgoing `QNetworkRequest` itself, from the header map and the `AuthScheme` of the active `ProviderProfile`. Each provider's profile carries both -- Claude `x-api-key` plus `anthropic-version`, the OpenAI-shaped clients `Authorization: Bearer`, Google a `key` query parameter, and every profile `Content-Type: application/json` -- and the caller overrides whatever it needs. `setHeader` sets one entry, `setHeaders` replaces the whole map, `setAuthScheme` moves the key to a different header or query parameter; all three edit the active profile, so `profile()` always shows what the next request will carry. An empty API key sends no credential at all.

**Thinking blocks.** `thinkingBlockReceived` has one source: `applyEffects` announcing the message's pending thinking blocks whenever a translator sets `MessageEffects::thinkingCompleted`. It walks the current blocks in wire order and announces each unannounced one exactly once, covering both `ThinkingContent` (text plus signature) and `RedactedThinkingContent` (signature only). The "already announced" set lives in `BaseMessage`, i.e. with the blocks -- there is no per-request counter in the client, and nothing resets across continuations because a new round produces new blocks. Translators accumulate reasoning into their message and raise the flag when a block is complete: when answer text starts arriving, and again at the finish reason.

**Token accounting.** Providers never build a `TokenUsage` themselves. A translator hands the object that carries usage back as `MessageEffects::usage`, and the base passes it to `applyUsage(id, root)`, which reads it through the provider's `UsageSchema` and merges what it found into the turn's snapshot. A counter the object did not mention is left alone, so the second usage report of a turn -- Claude's `message_delta`, every Gemini chunk, OpenAI's final chunk -- cannot zero a field the first one set. `applyUsage` returns without touching anything when the object says nothing about usage at all, which is what keeps `CompletionInfo::usage` a `nullopt` rather than a zeroed struct. The turn's snapshot is folded into the request total at each continuation, and the total is what reaches `requestFinalized`.

**Signal dispatch.** Text deltas, thinking blocks, tool start/result events, final completion, and errors are delivered as Qt signals (`chunkReceived`, `accumulatedReceived`, `thinkingBlockReceived`, `toolStarted`, `toolResultReady`, `requestCompleted`, `requestFinalized`, `requestFailed`). All signals are emitted on the `BaseClient`'s owning thread; Qt's default `AutoConnection` queues cross-thread delivery safely.

---

## Provider subclass contract

### Pure virtual

Public, the caller-facing surface:

- `sendMessage(payload, endpoint, mode)` -- put the provider's envelope on the payload (`stream`, `stream_options`, `store`) and hand it to `postJson`, which resolves the endpoint against the profile, logs, and sends. Nothing else belongs here.
- `listModels(endpoint)` -- almost always one line over `fetchModelList` with `profile().modelsPath`.

`ask()` is not on this list any more: both overloads live in `BaseClient`. The prompt form builds a one-turn `Conversation` and goes through the conversation form, so a provider that can answer a conversation can answer a prompt for free -- and cannot forget to attach tools to one of them.

`url()`, `apiKey()` and `model()` are the only way to read the endpoint triple, from inside the class as well as outside. They were protected fields until the thread guards on the accessors turned out to be decorative: fourteen reads in subclasses went straight past them. Nothing in the tree stores a copy.

Protected, the format-facing surface:

- `toolDialect()` -- the provider's `ToolDialect`, returned from its message translator (`FooMessage::toolDialect()`). This is what `ToolsManager` serializes tool definitions through. It is a method on the client rather than something read off the message object because `tools()` may be called before any request, i.e. before a translator exists.
- `usageSchema()` -- where the provider's token counters live: the name of the container object (empty for counters at the root), and for each of the four counters an optional nesting object plus a field name. A client that reports no usage returns `kNoUsageSchema`. Being pure virtual is the point: a new provider does not compile until it says how it spells usage.
- `processBufferedBody(id, body)` -- a whole non-streamed body, already parsed and already known to carry no error envelope. The base owns that prologue: it parses the bytes, fails the request on invalid JSON, fails it on any `error` member -- a non-empty string, or an object even when it has no `message` (then the object itself becomes the text) -- and only then calls this. A subclass never sees the raw bytes and never re-derives what a broken buffered response looks like. The usual body is one line: `applyEffects(id, ensureMessage<FooMessage>(id)->applyResponse(body))`.
- `buildContinuationPayload(originalPayload, message, toolResults)` -- the assistant turn plus the tool results, in the provider's wire format. `appendChatContinuation<FooMessage>` is the whole method for any provider whose turns are a plain `messages` array.

### Hooks with a working default

Override only when the provider deviates:

- `processSseEvent(id, event, json)` -- one framed SSE event, already parsed. This is where an SSE provider does its work; `event.type` carries the wire event name for providers that dispatch on it (Responses), and providers that dispatch on the JSON body (Claude, OpenAI, Google) ignore it.
- `processJsonLine(id, json)` -- one line of a JSON-lines stream, already parsed. The same role for a provider whose `streamFraming()` is `JsonLines` (Ollama).
- `streamFraming()` -- how this provider frames its stream: `ServerSentEvents` (the default) or `JsonLines` (Ollama). A request allocates exactly one framer, chosen from this, and only the base drives it: SSE events go to `processSseEvent`, JSON lines to `processJsonLine`, and whatever either framer still holds is flushed at end of stream. No subclass touches a framer, so none can ask for the wrong one. This is also why `BaseClient.hpp` does not drag `SSEParser.hpp` and `RpcLineFramer.hpp` into every translation unit that includes a client.
- `processData(id, data)` -- raw streaming bytes. The default feeds the request's framer. Override only to inspect the bytes first (Google sniffs for a non-SSE error body, then calls `BaseClient::processData`); a different framing is `streamFraming()`, not an override here.
- `errorAnnotations()` -- the provider's error-envelope vocabulary, as data. `errorMessageFrom(body)` reads `error.message` out of a decoded body and appends one parenthesised clause per annotation whose field is present; an annotation with an empty label prints the value bare. An `error` that is a plain string rather than an object (Ollama) is read as the message directly, so Ollama overrides nothing. `describeError(error)` is the same rendering for an error object on its own, with the object's JSON as the text when it has no message. The HTTP path, the buffered path and every error a translator reports from inside a stream go through these two functions, which is why they cannot drift.
- `parseHttpError(response)` -- the whole vendor error envelope, including the `"HTTP <status>: "` prefix and the fallback to a body snippet when there is no envelope at all. The default is built on `errorAnnotations()`; override it only for a provider whose errors are not JSON.
- `takePendingStreamError(id)` -- an error the provider recognised mid-stream but could not act on yet. The base drains it before concluding the stream succeeded. Google alone uses it, for the 200-with-error-body case its sniffer catches.
- `onStreamDrained(id)` -- the stream is drained and the request is still alive; last chance to finish the message off before the base reads its state. Google applies `thinkingCompleted` and `toolsReady` here, because its finish reason arrives inside a candidate rather than as an event of its own.
- `cleanupDerivedData(id)` -- per-request state beyond the message object. Only providers that keep survives-the-turn bookkeeping in the client need it (Google's failed-request set and error sniffers); state that belongs to one round lives in the translator and is dropped by `clearDerivedCaches()`.

The end-of-stream sequence itself (`onStreamFinished`) is private: the hooks above are the seams cut out of it.

Not a hook, but the same idea: `setProfile()` in the constructor is what tells the shared base code which paths, headers, auth scheme and logging category a provider uses. It replaces the whole identity at once; `setHeader()`, `setHeaders()` and `setAuthScheme()` then adjust the active profile, so the call order is always "profile first, adjustments after" and `setProfile(profile())` changes nothing. A client that derives from another (llama.cpp) starts from its parent's profile and overrides the fields it changes; a provider that changes *only* those fields does not need a class at all, which is why Mistral is `openAIProfile()` with three fields replaced. Every provider exports its profile (`claudeProfile()`, `openAIProfile()`, `openAIResponsesProfile()`, `googleProfile()`, `ollamaProfile()`, `llamaCppProfile()`, `mistralProfile()`), and each one carries its auth scheme, so adjusting a copy and handing it back never loses the key.

### End of stream

One sequence, shared by all seven providers:

1. a transport error, or `takePendingStreamError`, fails the request;
2. the base flushes whatever the framer still holds (a trailing SSE event, or a last JSON line without its newline);
3. `onStreamDrained` lets the provider finish the message;
4. a message left in `RequiresToolExecution` hands control to the tool loop and returns;
5. otherwise the stop reason is captured and the request completes.

Each step re-checks that the request is still alive, because any of them can emit a signal whose handler cancels it.

### Shared helpers the subclass calls

- `postJson(payload, endpoint, mode)` -- the only way to send: resolves an empty endpoint to `profile().chatPath`, appends it to `url()`, logs under the profile's category, and starts the request.
- `ensureMessage<T>(id)` / `messageForRequest(id)` and `applyEffects(id, effects)` -- a translator's `MessageEffects` is the one path from the wire to the signals; `applyEffects` applies text, thinking, usage, error and tools in one order for every provider.
- `fetchModelList(url, arrayKey, idKey, idMapper)` and `endpointUrl(endpoint, defaultPath)` -- model listing without a hand-rolled `QFuture`.
- `applyUsage` -- the whole of token accounting, see above.
- `addChunk`, `completeRequest`, `failRequest`, `cleanupFullRequest`, `hasRequest` -- for a stream that has no translator at all (llama.cpp's native `/completion` shape). `failRequest` releases everything the request holds, tool round included, whichever path calls it.
- `transport()` and `prepareNetworkRequest(url)` -- for requests outside the chat flow (llama.cpp's `/health` and `/props` probes).

### Typical sendMessage pattern

```cpp
RequestID FooClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);
    return postJson(request, endpoint, mode);
}
```

Nothing else is needed to get bytes flowing: the base frames them and calls the per-event hook, where the `FooMessage` for `id` is allocated on the first event that carries content and the translator's effects are applied.

```cpp
void FooClient::processSseEvent(
    const RequestID &id, const SSEEvent &, const QJsonObject &json)
{
    applyEffects(id, ensureMessage<FooMessage>(id)->applyEvent(json));
}
```

A provider whose stream can carry an event with no content after the turn has finished -- OpenAI's trailing usage chunk, a Gemini chunk without candidates -- reads the existing message with `messageForRequest(id)` for those events and calls `ensureMessage` only when content arrives, because `ensureMessage` starts a new round when the previous one ended in `RequiresToolExecution`.

`endpoint` lets the caller pick a non-default path on providers that expose more than one (e.g. Mistral's `/v1/fim/completions`). It is appended to the base URL exactly like the profile's `chatPath`, and an empty string selects that default.

Consumers subscribe to `BaseClient` signals (`chunkReceived`, `requestCompleted`, `requestFinalized`, `requestFailed`, `toolStarted`, `toolResultReady`, `thinkingBlockReceived`) to observe request progress. All signals are emitted on the `BaseClient`'s owning thread; Qt's `AutoConnection` handles cross-thread queued delivery.

A host that keeps its own conversation history must carry `CompletionInfo::requestPayload` forward rather than its original request: that field is the payload of the last turn actually sent, tool round-trips included.

---

## Error handling

Errors reach the caller through three paths:

- **Transport errors** -- DNS failures, timeouts, SSL errors, aborted connections, and connection refused. These originate from the network layer and are forwarded as failure notifications.
- **HTTP errors (4xx/5xx)** -- When non-success status headers arrive, the client switches to error mode and accumulates the response body. At stream end, `parseHttpError` gets a chance to read the vendor-specific error format. If it declines, a default message with the status code and a body snippet is used.
- **Provider errors inside a successful response** -- an `error` member in a buffered 2xx body, an `error` event (Anthropic, Responses), a `response.failed`, an `error` object in a streamed chunk or line (OpenAI-compatible servers, Google, Ollama), or a blocking finish reason (Google's safety filters). The buffered prologue catches the first; a translator hands the others back as `MessageEffects::error`, and `applyEffects` delivers whatever text came with the event before failing the request with the same rendering the HTTP path uses.

---

## Checklist: adding a new provider

1. Create a new folder under `source/clients/` with the client and message translator files.
2. Add a public header under `include/LLMQore/`, and list it in the `include/LLMQore/Clients` umbrella header.
3. In the translator's `.cpp`, define the provider's `ToolDialect` subclass (anonymous namespace) and expose it through a static `FooMessage::toolDialect()`. Both directions of the format -- schema out, tool results back -- belong in this one file.
4. Implement the pure virtuals: `sendMessage`, `buildConversationPayload`, `listModels`, `toolDialect`, `usageSchema`, `processBufferedBody`, `buildContinuationPayload`. The usage schema is a `constexpr UsageSchema` next to the client, like the dialect is next to the translator. Paths, auth and headers go into an exported `fooProfile()` -- there is no request-building hook to override.
5. Call `setProfile(fooProfile())` in the constructor so the shared base code resolves endpoints, authenticates and logs under the new provider's name, and override `processSseEvent()` for the streaming path. A provider that is not SSE-framed answers `streamFraming()` with `JsonLines` and overrides `processJsonLine()` instead.
6. Give the translator `applyEvent()` / `applyResponse()` returning `MessageEffects`, and hand them to `applyEffects` from the stream handler through `ensureMessage<FooMessage>(id)`; do not keep a message map in the client.
7. Add unit tests: constructor sanity and header shape (`tst_RequestHeaders` is parameterised over every provider), the tool schema shape in `tst_ToolsManager`, model listing over `FakeHttpTransport` in `ListModels`, error rendering in `ParseHttpError`, and translator behaviour in a `tst_FooMessage` suite that needs no event loop.
