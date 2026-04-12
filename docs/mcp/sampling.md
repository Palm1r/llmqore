# Sampling (`sampling/createMessage`)

Sampling is MCP's reverse-direction tool-call: an MCP **server** hands
the **client** a ready-to-run prompt and asks "please run this through
your LLM for me and return the completion". Typical use cases are
agentic servers that own task logic but don't own an LLM themselves —
they delegate inference back to whatever model the client is already
using.

LLMCore wires sampling straight through an existing `BaseClient`: you
hand the `McpClient` a live provider client (your `ClaudeClient`,
`OpenAIClient`, ...) plus a small **payload-builder** lambda that
translates the MCP wire shape into that provider's payload, and the
library drives the full inference and tool loop under the hood.
There is **no** `BaseSamplingProvider` subclass to write — an earlier
revision of this doc described one, but the abstraction was removed in
the sampling refactor; see [`architecture/sampling.md`](architecture/sampling.md)
for the rationale.

This document covers:

1. The protocol shape and types
2. Handling sampling on the client: wiring a provider client and a
   payload-builder lambda
3. Initiating sampling on the server
4. A worked `ClaudeClient` builder and the things a real host should
   add on top (consent UI, rate limiting, ...)
5. End-to-end flow over HTTP
6. Testing

All snippets compile against the public LLMCore headers. The
end-to-end round-trip is covered by the three
`McpLoopbackTest.Sampling*` cases in `tests/tst_McpLoopback.cpp`.

---

## 1. The wire shape

```
request:  method = "sampling/createMessage"
          params = CreateMessageParams

response: CreateMessageResult
```

The types live in `<LLMCore/McpTypes.hpp>`:

```cpp
struct SamplingMessage {
    QString role;        // "user" | "assistant"
    QJsonObject content; // {type, text/data/mimeType}
};

struct ModelHint {
    QString name;
};

struct ModelPreferences {
    QList<ModelHint>          hints;
    std::optional<double>     costPriority;
    std::optional<double>     speedPriority;
    std::optional<double>     intelligencePriority;
};

struct CreateMessageParams {
    QList<SamplingMessage>          messages;
    std::optional<ModelPreferences> modelPreferences;
    QString                         systemPrompt;
    QString                         includeContext; // "none" | "thisServer" | "allServers"
    std::optional<double>           temperature;
    int                             maxTokens = 0;
    QStringList                     stopSequences;
    QJsonObject                     metadata;
};

struct CreateMessageResult {
    QString     role;       // conventionally "assistant"
    QJsonObject content;    // one content block (text/image/audio)
    QString     model;      // which model actually answered
    QString     stopReason; // raw provider stop/finish reason, unnormalised
};
```

`SamplingMessage::content` and `CreateMessageResult::content` are
opaque `QJsonObject`s on purpose — the MCP spec constrains them to
the same text/image/audio shape as `ToolContent`, so a host can reuse
`LLMCore::ToolContent::toJson()` / `fromJson()` when translating
between LLMCore's internal types and the sampling wire format.

---

## 2. Handling sampling on the client

The client is where "the LLM" actually lives. To answer incoming
`sampling/createMessage` requests, wire an existing `BaseClient` and
a `SamplingPayloadBuilder` lambda into the `McpClient`:

```cpp
#include <LLMCore/ClaudeClient.hpp>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpTypes.hpp>

using namespace LLMCore::Mcp;

auto *claude = new LLMCore::ClaudeClient(apiKey, endpoint, "claude-sonnet-4-6");

// Payload builder: MCP CreateMessageParams → provider-specific JSON.
// The lambda is copied into the McpClient; the BaseClient is held as
// QPointer and must outlive any sampling call.
McpClient::SamplingPayloadBuilder buildClaudePayload
    = [](const CreateMessageParams &p) -> QJsonObject {
        QJsonArray messages;
        for (const SamplingMessage &m : p.messages) {
            messages.append(QJsonObject{
                {"role",    m.role},
                {"content", QJsonArray{m.content}},
            });
        }
        QJsonObject payload{
            {"model",      "claude-sonnet-4-6"},
            {"max_tokens", p.maxTokens > 0 ? p.maxTokens : 1024},
            {"messages",   messages},
            {"stream",     true},
        };
        if (!p.systemPrompt.isEmpty())
            payload["system"] = p.systemPrompt;
        if (p.temperature.has_value())
            payload["temperature"] = *p.temperature;
        if (!p.stopSequences.isEmpty()) {
            QJsonArray stops;
            for (const QString &s : p.stopSequences)
                stops.append(s);
            payload["stop_sequences"] = stops;
        }
        return payload;
    };

client->setSamplingClient(claude, buildClaudePayload);
```

That's it. After this call:

1. The next handshake declares the `sampling` capability, so servers
   know they may call `sampling/createMessage`.
2. Incoming `sampling/createMessage` requests are handled internally:
   the library parses the request into `CreateMessageParams`, runs
   your builder, feeds the result to the provider client, waits for
   the completion to finalize (see
   [`architecture/sampling.md`](architecture/sampling.md) for why a
   dedicated completion callback was needed), and wraps the text +
   model + stop reason into a `CreateMessageResult`.
3. Tool-calling inside the sampled completion works automatically:
   every tool registered in the provider client's `ToolsManager`
   (local tools plus remote MCP tools bridged via `McpToolBinder`) is
   available to the sampled call, and the continuation loop runs for
   up to the usual ten rounds before the completion finalizes.

Without a sampling client wired up, the `McpClient` answers incoming
`sampling/createMessage` requests with `MethodNotFound (-32601)` —
the spec-mandated response for an unsupported server-to-client method.

### Refusing or failing a sampling request

Errors take two shapes, depending on where they originate:

- **Builder threw / returned an invalid payload** — the exception
  propagates as `McpRemoteError(InternalError, ...)` to the server.
  Use this if your builder wants to reject a request outright (e.g.
  unsupported model hint, content block type you don't translate).
- **Provider-side failure** — if the LLM request fails, the error is
  mapped to `McpRemoteError(InternalError, <text>)`. This is how
  transport or provider-side failures reach the server as a JSON-RPC
  `error` envelope instead of a timeout.

Verified end-to-end by `tst_McpLoopback.SamplingClientErrorPropagatesAsRemoteError`.

### What the library does NOT handle for you

The payload builder is a **policy** seam. Things you still have to
decide yourself, on top of the minimal shape above:

- **Consent UI.** Pop a dialog the first time each server requests
  sampling, cache "always allow" / "always deny" per server, keyed on
  the server's name and version from the handshake.
- **Rate limiting / quota.** Cap the number of sampling round-trips
  per minute so a runaway server can't drain the user's model quota.
- **Image / audio handling.** If the server passes image blocks in
  `params.messages`, translate them in the builder. Claude's shape
  is close: lift `{data, mimeType}` into `{source: {type: "base64",
  media_type, data}}`. OpenAI Responses uses `input_image` items;
  Gemini uses `inlineData` parts.
- **Tools / toolChoice pass-through (2025-11-25).** The spec added
  `tools` / `toolChoice` params to `sampling/createMessage`. LLMCore
  doesn't yet expose them as typed fields — they live in
  `CreateMessageParams::metadata` verbatim and your builder can pick
  them out and forward them to the provider's tools array. See
  [`mcp_protocol_coverage.md`](mcp_protocol_coverage.md) §4.
- **Model choice.** `CreateMessageParams::modelPreferences.hints` is
  advisory. The builder above ignores it and pins
  `claude-sonnet-4-6`; most real hosts want to consult hints and
  fall back to a configured default. See the
  [full bridge pattern](#4-full-claudeclient-bridge-pattern) below.

---

## 3. Initiating sampling on the server

The server side is symmetric: after the handshake, ask the client to
sample. The request short-circuits with `McpProtocolError` if:

- the server is not yet initialized, or
- the connected client did not advertise the `sampling` capability
  during `initialize`.

Otherwise it is sent to the client as a normal JSON-RPC request:

```cpp
#include <LLMCore/Mcp>

// Somewhere inside a server-side tool or background task:
CreateMessageParams params;

SamplingMessage msg;
msg.role    = "user";
msg.content = QJsonObject{
    {"type", "text"},
    {"text", "Summarise this error log in one sentence:\n\n" + logLines}};
params.messages = {msg};

params.systemPrompt = "You are a terse error-log summariser.";
params.maxTokens    = 256;
params.temperature  = 0.2;

// Optional: hint at a preferred model family. The client is free to
// ignore these.
ModelHint h;
h.name = "claude-sonnet";
ModelPreferences prefs;
prefs.hints                = {h};
prefs.intelligencePriority = 0.9;
params.modelPreferences    = prefs;

server->createSamplingMessage(params, std::chrono::seconds(60))
    .then(this, [](const CreateMessageResult &r) {
        const QString summary = r.content.value("text").toString();
        qInfo().noquote() << "[" << r.model << "]" << summary;
    })
    .onFailed(this, [](const McpRemoteError &e) {
        qWarning() << "sampling refused:" << e.code() << e.remoteMessage();
    })
    .onFailed(this, [](const McpException &e) {
        qWarning() << "sampling failed:" << e.message();
    });
```

Typical timeouts are generous (default: 120 s) because sampling
involves a real LLM round-trip plus, potentially, a user consent UI
on the client side.

### Tool-calling inside sampling (MCP 2025-11-25)

Works end-to-end on the **execution** side with zero extra code: any
tool the host's `BaseClient` has in its `ToolsManager` is callable
during the sampled turn (including remote tools from *other* MCP
servers bound via `McpToolBinder`), and the continuation loop runs
under the covers.

What's not yet plumbed is the **parameter** side — the
`tools`/`toolChoice` fields a server can send in
`sampling/createMessage` to declare *which* tools the sampled
completion is allowed to call. LLMCore currently preserves them in
`CreateMessageParams::metadata` verbatim; the host builder has to
pick them out and forward them to the provider's tools array. Tracked
in [`mcp_protocol_coverage.md`](mcp_protocol_coverage.md) §4.

---

## 4. Full `ClaudeClient` bridge pattern

The reference pattern. Nothing in LLMCore assumes this shape — it's
one of many valid ways to build a payload. Compared to the minimal
lambda in §2, this version consults `modelPreferences`, falls back
to a configured default, and pulls the model ID into a captured
variable so the `BaseClient` doesn't have to be reconfigured per call.

```cpp
#include <LLMCore/ClaudeClient.hpp>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpTypes.hpp>

using namespace LLMCore::Mcp;

static QString pickClaudeModel(
    const std::optional<ModelPreferences> &prefs,
    const QString &defaultModel)
{
    if (!prefs.has_value())
        return defaultModel;
    for (const ModelHint &h : prefs->hints) {
        if (h.name.contains("haiku", Qt::CaseInsensitive))
            return "claude-haiku-4-5-20251001";
        if (h.name.contains("sonnet", Qt::CaseInsensitive))
            return "claude-sonnet-4-6";
        if (h.name.contains("opus", Qt::CaseInsensitive))
            return "claude-opus-4-6";
    }
    return defaultModel;
}

void wireClaudeSampling(
    LLMCore::Mcp::McpClient *mcp,
    LLMCore::ClaudeClient *claude,
    QString defaultModel)
{
    McpClient::SamplingPayloadBuilder builder
        = [defaultModel = std::move(defaultModel)]
          (const CreateMessageParams &p) -> QJsonObject {

        QJsonArray messages;
        for (const SamplingMessage &m : p.messages) {
            messages.append(QJsonObject{
                {"role",    m.role},
                {"content", QJsonArray{m.content}},
            });
        }

        QJsonObject payload{
            {"model",      pickClaudeModel(p.modelPreferences, defaultModel)},
            {"max_tokens", p.maxTokens > 0 ? p.maxTokens : 1024},
            {"messages",   messages},
            {"stream",     true},
        };
        if (!p.systemPrompt.isEmpty())
            payload["system"] = p.systemPrompt;
        if (p.temperature.has_value())
            payload["temperature"] = *p.temperature;
        if (!p.stopSequences.isEmpty()) {
            QJsonArray stops;
            for (const QString &s : p.stopSequences)
                stops.append(s);
            payload["stop_sequences"] = stops;
        }
        return payload;
    };

    mcp->setSamplingClient(claude, std::move(builder));
}
```

Call it once at startup after you've constructed your `ClaudeClient`
and `McpClient`:

```cpp
auto *claude = new LLMCore::ClaudeClient(apiKey, endpoint, "claude-sonnet-4-6");
auto *mcp    = new LLMCore::Mcp::McpClient(transport);

wireClaudeSampling(mcp, claude, "claude-sonnet-4-6");

// ... then connectAndInitialize() as usual; the sampling capability
// will be declared on the outbound initialize.
mcp->connectAndInitialize();
```

> **Reminder:** the `BaseClient` and the `McpClient` must both
> outlive any in-flight sampling call. `McpClient` holds the sampling
> client as a `QPointer`, so deletion races bail out cleanly, but
> there is no automatic reconnect.

---

## 5. End-to-end flow over HTTP

When the client uses `McpHttpTransport` and the server uses
`McpHttpServerTransport`, sampling still works — but only by
piggybacking on POST responses. There is no long-lived `GET /mcp`
server-push channel (explicitly out of scope, see
[`mcp_protocol_coverage.md`](mcp_protocol_coverage.md) §2 and
[`architecture/http-transport.md`](architecture/http-transport.md)
for the buffering mechanism).

The sequence is:

1. **Client to Server**: POST any request (e.g. `tools/call`).
2. **Server**: handler starts, issues a sampling request via the
   session. The transport has no matching pending socket, so it
   queues the message.
3. **Server**: the original `tools/call` handler's future resolves.
   The transport writes the queued sampling request AND the
   tools/call response as a `text/event-stream` response body in
   that order.
4. **Client**: parses the SSE stream, sees both messages,
   dispatches the `tools/call` response to the waiting future AND
   routes the sampling request through the sampling handler (which
   runs the builder, drives the provider client, waits for the
   completion to finalize).
5. **Client to Server**: POSTs the sampling result as a brand new
   request (with the server-allocated id).
6. **Server transport**: sees an incoming JSON-RPC response shape,
   routes it back through the session layer, which resolves the
   pending sampling future.
7. The server's original handler resumes.

In stdio and pipe transports the flow is simpler — every message
is delivered immediately in both directions, no buffering. The
application code is identical.

---

## 6. Testing

The three loopback tests in `tests/tst_McpLoopback.cpp` demonstrate
every code path you are likely to care about:

- `SamplingRoundTripsFromServerToClient` — happy path. The server
  requests sampling; the client's handler runs the builder and feeds
  the payload to a fake provider client; the server's future resolves
  with the expected text, model name, and raw stop reason.
- `SamplingWithoutClientFailsCapabilityGuard` — guard path. No
  sampling client wired up, so the `sampling` capability is never
  advertised; the server's call fails fast with `McpProtocolError`
  before hitting the wire.
- `SamplingClientErrorPropagatesAsRemoteError` — error path. The
  fake client fires a canned error; the typed
  `McpRemoteError(InternalError, ...)` propagates unchanged through
  the session and the server receives the matching error code.

Both the types and the capability flag round-trip via
`tst_McpTypes.cpp` — see `SamplingMessageAndParamsRoundTrip`,
`CreateMessageResultRoundTrip`, and
`ClientCapabilitiesPreserveSamplingFlag`.

---

## See also

- [`client-usage.md`](client-usage.md) — general client-side overview,
  including how sampling fits alongside roots and elicitation providers.
- [`server-hosting.md`](server-hosting.md) — how to build a server
  that can request sampling from its connected client.
- [`architecture/sampling.md`](architecture/sampling.md) —
  architectural notes: why there is no `BaseSamplingProvider`,
  how the completion callback plumbs the stop reason through, and the
  end-to-end flow.
- [`mcp_protocol_coverage.md`](mcp_protocol_coverage.md) §4 — current status of
  sampling coverage, known gaps, and pointers to tests.
