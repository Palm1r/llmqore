# Using MCP servers from LLMCore

This document shows how to consume an MCP server from an LLMCore-based
application: connecting a transport, performing the handshake, calling
tools, reading resources, and bridging remote tools into a
`ToolsManager` so any `BaseClient` (Claude, OpenAI, Gemini, Ollama, …)
can call them as if they were local.

Every snippet compiles against the public LLMCore headers and mirrors
patterns already used by `example/mcp-http-probe/main.cpp`,
`example/ChatController.cpp`, and the `tst_McpLoopback` tests — look
there when you need a runnable starting point.

---

## 1. Minimum end-to-end: connect, list, call

The three-step "smoke test" shape: pick a transport, hand it to an
`McpClient`, perform the handshake, then issue whatever request you
need. Every request returns a `QFuture<T>` so you can chain
continuations with `.then()` / `.onFailed()`.

```cpp
#include <LLMCore/Mcp>

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QJsonObject>

using namespace LLMCore::Mcp;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // --- 1. Pick a transport (HTTP 2025-03-26+ shown here) ---
    HttpTransportConfig cfg;
    cfg.endpoint = QUrl("http://127.0.0.1:8080/mcp");
    cfg.spec     = McpHttpSpec::V2025_03_26;

    auto *transport = new McpHttpTransport(cfg, &app);

    // --- 2. Build an McpClient over that transport ---
    auto *client = new McpClient(
        transport, Implementation{"my-app", "0.1.0"}, &app);

    // --- 3. Handshake, then list + call a tool ---
    client->connectAndInitialize(std::chrono::seconds(10))
        .then(&app, [client](const InitializeResult &info) {
            qInfo() << "connected to" << info.serverInfo.name;
            return client->listTools();
        })
        .unwrap() // QFuture<QFuture<T>> -> QFuture<T>
        .then(&app, [client](const QList<ToolInfo> &tools) {
            for (const ToolInfo &t : tools)
                qInfo().noquote() << " -" << t.name << ":" << t.description;

            return client->callTool(
                "echo", QJsonObject{{"text", "hello"}});
        })
        .unwrap()
        .then(&app, [](const LLMCore::ToolResult &r) {
            qInfo().noquote() << "result:" << r.asText();
            QCoreApplication::quit();
        })
        .onFailed(&app, [](const std::exception &e) {
            qCritical() << "failed:" << e.what();
            QCoreApplication::quit();
        });

    return app.exec();
}
```

Key points:

- **Handshake** — the client starts the transport, negotiates
  capabilities with the server, and resolves once the connection is
  ready. Subsequent requests fail fast if the handshake has not
  completed.
- **Continuations** always take a `QObject*` context so the callback
  is cancelled when the context goes away. `&app` is fine for
  top-level code; inside a class pass `this`.
- **Unwrapping** flattens nested futures so chained requests compose
  naturally.
- **Errors** arrive as typed exceptions:
  - `McpRemoteError` — the server replied with a JSON-RPC error envelope.
  - `McpTimeoutError` — the request timed out.
  - `McpTransportError` — the transport died / couldn't open.
  - `McpProtocolError` — a client-side precondition failed
    (e.g. "not initialized").
  Catch them in order of increasing generality in `.onFailed()`
  chains; the base is `McpException`.

---

## 2. Transport selection cheat sheet

All transports inherit from `McpTransport` — the rest of the library
only sees that interface, so swapping transports is a one-line change.

### Launch a child process over stdio

For MCP servers distributed as executables. Paths to `.cmd` / `.bat`
files and `npx`-style launchers are auto-wrapped in `cmd.exe /c` on
Windows.

```cpp
#include <LLMCore/McpStdioTransport.hpp>

StdioLaunchConfig launch;
launch.program   = "C:/path/to/example-mcp-server.exe";
launch.arguments = {"--verbose"};
// Optional: customise env and cwd.
launch.environment = QProcessEnvironment::systemEnvironment();
launch.environment.insert("MCP_LOG_LEVEL", "debug");
launch.workingDirectory = "C:/work";
launch.startupTimeoutMs = 10000;

auto *transport = new McpStdioClientTransport(launch, &app);
auto *client    = new McpClient(transport, Implementation{"my-app", "0.1.0"}, &app);
```

The child's stderr is forwarded to the `stderrLine(QString)` signal on
the transport; connect it to a log widget if you want to surface MCP
server logs to your user.

### Connect to an HTTP server

Two wire formats are supported, both selected by
`HttpTransportConfig::spec`:

```cpp
#include <LLMCore/McpHttpTransport.hpp>

HttpTransportConfig cfg;
cfg.endpoint = QUrl("http://127.0.0.1:3001/sse");

// Legacy two-endpoint HTTP+SSE (spec 2024-11-05). The client opens a
// long-lived GET, learns the POST endpoint from an `endpoint` SSE event,
// and uses it for every subsequent request. Used by older MCP servers
// (e.g. Qt Creator 19.0 — mcp-http-probe --spec 2024-11-05 ... /sse).
cfg.spec = McpHttpSpec::V2024_11_05;

// Single-endpoint Streamable HTTP (spec 2025-03-26 and newer). Each
// JSON-RPC message is POSTed; responses arrive as application/json or
// a short-lived SSE stream.
cfg.spec = McpHttpSpec::V2025_03_26;

// Optional custom headers (useful for API keys / bearer tokens).
cfg.headers.insert("Authorization", "Bearer <token>");
cfg.requestTimeoutMs = 120000;

auto *transport = new McpHttpTransport(cfg, &app);
```

Which spec should I pick? Read the server's documentation — if it
offers two endpoints (`/sse` + a POST URL resolved at runtime), it's
2024-11-05. If it exposes a single POST endpoint, it's 2025-03-26+.
The `McpHttpSpec::Latest` alias resolves to the newest supported
revision so you can write forward-compatible code.

### In-process loopback (testing / embedded)

Useful when you want a client and a server running in the same
process — mainly for tests, but also for embedding an
LLMCore-hosted MCP server side-by-side with a client that consumes
it. Deliver is via queued signal/slot connections, so both sides run
on the same Qt event loop.

```cpp
#include <LLMCore/McpPipeTransport.hpp>

auto [serverTransport, clientTransport] = McpPipeTransport::createPair();

McpServer server(serverTransport, McpServerConfig{});
McpClient client(clientTransport, Implementation{"loopback", "0.1.0"});

server.start();
client.connectAndInitialize();
// Both transports are owned by the caller; delete them when done.
```

This is the shape used by every `tst_McpLoopback` test.

---

## 3. Bridging remote tools into a `ToolsManager`

The point of MCP for an LLMCore-based app is usually "make the tools
on a remote server callable by my LLM provider client". The
`McpToolBinder` does that in one call:

```cpp
#include <LLMCore/Mcp>
#include <LLMCore/ToolsManager.hpp>
#include <LLMCore/ToolSchemaFormat.hpp>

using namespace LLMCore;

// Your existing ToolsManager that your BaseClient already reads from.
ToolsManager tools(ToolSchemaFormat::Claude);

// Build an McpClient as in section 1 — any transport works.
auto *client = new Mcp::McpClient(/*transport*/ ..., Mcp::Implementation{"my-app", "0.1.0"});

// Hand client + manager to the binder. It will:
//   - call connectAndInitialize() if the client isn't yet initialized
//   - call listTools() and wrap each ToolInfo in an McpRemoteTool
//   - addTool() each wrapper into the ToolsManager
//   - listen for notifications/tools/list_changed and re-sync
Mcp::McpToolBinder binder(client, &tools);

binder.bind()
    .then(this, []() {
        // Tools are now visible to the ToolsManager.
    })
    .onFailed(this, [](const std::exception &e) {
        qWarning() << "bind failed:" << e.what();
    });

// You can inspect what the binder currently has registered:
QStringList names = binder.registeredToolNames();
```

The binder only keeps a `QPointer` to its inputs — it does not own the
client or the manager, and it must outlive neither. In the chat
example, each `McpClient` has its own long-lived binder attached to
the session's `ToolsManager`.

**Why use `McpRemoteTool` directly?** If you want to manage the
registration lifecycle yourself (e.g. to batch tool registration from
multiple servers, or to filter by tool name) you can skip the binder
and wire the adapter by hand:

```cpp
for (const ToolInfo &info : client->cachedTools()) {
    if (info.name.startsWith("debug_"))
        continue; // filter
    tools.addTool(new Mcp::McpRemoteTool(client, info));
}
```

Each `McpRemoteTool` forwards execution to the remote server and
preserves the full `ToolResult` envelope — image / audio / resource
content blocks come through unchanged, so a model that supports rich
tool results can see them in the next continuation turn.

---

## 4. Resources and prompts

MCP resources and prompts behave symmetrically to tools:

```cpp
#include <LLMCore/Mcp>
using namespace LLMCore::Mcp;

// --- Resources ---
client->listResources()
    .then(&app, [client](const QList<ResourceInfo> &list) {
        for (const ResourceInfo &r : list)
            qInfo() << r.uri << r.mimeType;

        // Read a specific resource by URI.
        return client->readResource("file:///etc/motd");
    })
    .unwrap()
    .then(&app, [](const ResourceContents &c) {
        if (!c.text.isEmpty())
            qInfo() << "text resource:" << c.text;
        else
            qInfo() << "binary resource:" << c.blob.size() << "bytes";
    });

// Subscribe to change notifications (server must support it).
client->subscribeResource("file:///etc/motd");
QObject::connect(client, &McpClient::resourceUpdated, &app, [](const QString &uri) {
    qInfo() << "resource changed:" << uri;
});

// --- Prompts ---
client->listPrompts()
    .then(&app, [client](const QList<PromptInfo> &list) {
        for (const PromptInfo &p : list)
            qInfo() << p.name << p.description;

        // Render a prompt with arguments substituted.
        return client->getPrompt(
            "code_review",
            QJsonObject{{"language", "cpp"}, {"style", "terse"}});
    })
    .unwrap()
    .then(&app, [](const PromptGetResult &r) {
        for (const PromptMessage &m : r.messages)
            qInfo().noquote() << m.role << ":" << m.content;
    });
```

**URI-templated resources** (`resources/templates/list`, RFC 6570
placeholders like `file:///logs/{date}.log`) are listed separately.
Clients typically use them to drive autocompletion over
`completion/complete` — see section 6.

---

## 5. Cancellation and progress

A simple tool call is fine for fire-and-forget use. If you need
cancellation or progress updates, use the progress-aware variant:

```cpp
auto call = client->callToolWithProgress(
    "long_running_task",
    QJsonObject{{"input", "..."}},
    [](double progress, double total, const QString &message) {
        // progress / total come from the server via notifications/progress.
        // Either may be 0 if the server chose not to provide a quantity.
        qInfo() << "progress:" << progress << "/" << total << message;
    });

// Kick off a cancel from the UI:
QPushButton *cancelBtn = ...;
QObject::connect(cancelBtn, &QPushButton::clicked, &app, [&call, client]() {
    client->cancel(call.requestId, "user clicked cancel");
});

// Consume the result — on cancellation the future completes with
// McpCancelledError.
call.future
    .then(&app, [](const LLMCore::ToolResult &r) {
        qInfo() << "result:" << r.asText();
    })
    .onFailed(&app, [](const McpCancelledError &) {
        qInfo() << "cancelled";
    });
```

Under the hood the library tags the outbound request with a progress
token, registers the callback internally, and clears it when the
future finishes (normally or on cancel).

---

## 6. Autocompletion (`completion/complete`)

If the server advertises the `completions` capability you can request
completions for a prompt argument or a resource template placeholder
as the user types:

```cpp
CompletionReference ref;
ref.type = "ref/prompt";     // or "ref/resource"
ref.name = "code_review";    // prompt name (for ref/prompt)
// For ref/resource use ref.uri = "file:///logs/{date}.log" instead.

client->complete(
    ref,
    /*argumentName=*/"language",
    /*partialValue=*/"py",
    // Optional: other arguments the user has already filled in, so
    // the server can offer dependent-value suggestions.
    QJsonObject{{"framework", "django"}})
    .then(&app, [](const CompletionResult &r) {
        for (const QString &v : r.values)
            qInfo() << "-" << v;
        if (r.total) qInfo() << "total:" << *r.total;
        if (r.hasMore) qInfo() << "(more available)";
    });
```

Completion is advisory: providers that can't answer return an empty
list rather than throwing, so the future almost never fails.

---

## 7. Responding to server-initiated calls

An MCP server can also reach back into the client for a few specific
operations. LLMCore lets you plug providers in so the client can
handle these requests:

### Roots (`roots/list`) — expose allowed working directories

```cpp
#include <LLMCore/BaseRootsProvider.hpp>

class AppRootsProvider : public Mcp::BaseRootsProvider
{
    Q_OBJECT
public:
    QFuture<QList<Mcp::Root>> listRoots() override {
        QList<Mcp::Root> out;
        Mcp::Root r;
        r.uri  = "file:///home/me/workspace";
        r.name = "workspace";
        out.append(r);

        auto p = std::make_shared<QPromise<QList<Mcp::Root>>>();
        p->start();
        p->addResult(out);
        p->finish();
        return p->future();
    }
};

AppRootsProvider roots;
client->setRootsProvider(&roots);

// Emit listChanged() whenever the allowed directory list changes.
// McpClient forwards it as notifications/roots/list_changed.
```

Declaring a provider also declares the `roots` capability on the next
`initialize` handshake; without one, the client answers `roots/list`
with an empty list.

### Sampling (`sampling/createMessage`) — let the server use your LLM

The server can ask the client to run a prompt through its LLM. LLMCore
wires sampling straight through an existing `BaseClient`: hand the
`McpClient` a live provider client plus a `SamplingPayloadBuilder`
lambda that translates the MCP wire shape into that provider's
payload, and the library drives the full inference and tool loop
under the hood. Which model handles which server, consent UI, rate
limiting, image-block translation — those remain policy decisions
for the host. See [`sampling.md`](sampling.md) for the full story;
the minimal shape is:

```cpp
#include <LLMCore/ClaudeClient.hpp>
#include <LLMCore/McpClient.hpp>
#include <LLMCore/McpTypes.hpp>

auto *claude = new LLMCore::ClaudeClient(apiKey, endpoint, "claude-sonnet-4-6");

Mcp::McpClient::SamplingPayloadBuilder builder
    = [](const Mcp::CreateMessageParams &p) -> QJsonObject {
        // Build a provider-specific payload from the MCP wire params.
        // Refuse a request by throwing from inside the lambda — the
        // exception surfaces to the server as McpRemoteError(InternalError).
        QJsonArray messages;
        for (const Mcp::SamplingMessage &m : p.messages) {
            messages.append(QJsonObject{
                {"role",    m.role},
                {"content", QJsonArray{m.content}},
            });
        }
        return QJsonObject{
            {"model",      "claude-sonnet-4-6"},
            {"max_tokens", p.maxTokens > 0 ? p.maxTokens : 1024},
            {"messages",   messages},
            {"stream",     true},
            {"system",     p.systemPrompt},
        };
    };

client->setSamplingClient(claude, builder);
```

Wiring a sampling client declares the `sampling` capability on the
next handshake; without one, the client answers
`sampling/createMessage` with `MethodNotFound`. Tool-calling inside
sampling comes for free — all tools registered in the provider
client's `ToolsManager` (local tools plus remote tools from other MCP
servers via `McpToolBinder`) are available to the sampled call.

---

## 8. Logging from the server

If the server advertises the `logging` capability you can set a
minimum log level and receive server-emitted log messages via signal:

```cpp
client->setLogLevel(LogLevel::Info); // or "debug", "warning", ...
QObject::connect(
    client, &McpClient::logMessage,
    &app, [](const QString &level, const QString &logger,
             const QJsonValue &data, const QString &message) {
        qInfo().noquote() << "[MCP/" << logger << "/" << level << "]" << message;
    });
```

This is a separate channel from LLMCore's own `QLoggingCategory`
system (`llmMcpLog`), which logs library internals. The MCP logging
channel is for application-level logs the server wants to expose to
the connected client.

---

## 9. Shutdown

Just drop the transport. Shutting down stops the transport and clears
the initialized flag; the destructor does the same if you rely on
Qt's parent-child ownership:

```cpp
client->shutdown();
// Or, equivalently, just delete the parent that owns both the client
// and the transport — no extra cleanup required.
```

The session aborts every pending request with `McpTransportError` when
the transport closes, so any in-flight `QFuture<T>` resolves with a
typed exception rather than hanging.

---

## See also

- [`server-hosting.md`](server-hosting.md) — building your own MCP
  server with LLMCore (stdio or HTTP).
- [`sampling.md`](sampling.md) — end-to-end sampling flow, including
  a full `ClaudeClient` `SamplingPayloadBuilder` and the policy knobs
  (consent, rate limit, image translation) a real host should add.
- [`architecture.md`](architecture.md) — the full library
  architecture, including the class diagram and async chain model.
- `example/mcp-http-probe/main.cpp` — a runnable smoke test you can
  copy-paste as a starting point.
- `tests/tst_McpLoopback.cpp` — exhaustive coverage of every client
  method, wired via the in-process `McpPipeTransport`.
