# Creating an MCP server with LLMCore

This document shows how to build an MCP server using LLMCore: writing
tools, exposing resources and prompts, and running the whole thing over
stdio (for use with Claude Desktop, Claude Code CLI, Cursor, …) or over
HTTP (for local dashboards, side-car processes, or any non-stdio host).

Every snippet compiles against the public LLMCore headers and mirrors
patterns used by `example/mcp-server-demo/main.cpp`,
`example/ExampleTools.hpp`, the `tst_McpLoopback` tests, and
`tst_McpHttpServer`.

---

## 1. The smallest possible server

An MCP server in LLMCore is three objects wired together:

1. a **transport** (stdio / HTTP / in-process pipe)
2. an **`McpServer`** — the JSON-RPC layer that handles `initialize`,
   `tools/list`, `tools/call`, and the rest of the spec
3. one or more **`BaseTool`** subclasses (and optionally
   `BaseResourceProvider` / `BasePromptProvider`)

The whole thing is a normal `QCoreApplication`:

```cpp
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrent>

#include <LLMCore/Mcp>
#include <LLMCore/Tools>

using namespace LLMCore;
using namespace LLMCore::Mcp;

// --- 1. Write a tool -----------------------------------------------------

class EchoTool : public BaseTool
{
    Q_OBJECT
public:
    explicit EchoTool(QObject *parent = nullptr) : BaseTool(parent) {}

    QString id()          const override { return "echo"; }
    QString displayName() const override { return "Echo"; }
    QString description() const override { return "Returns the input text verbatim."; }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{{"text", QJsonObject{{"type", "string"}}}}},
            {"required",   QJsonArray{"text"}},
        };
    }

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        const QString text = input.value("text").toString();
        return QtConcurrent::run([text]() -> ToolResult {
            return ToolResult::text(QString("echo: %1").arg(text));
        });
    }
};

// --- 2. Stand up the server ---------------------------------------------

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Stdio: reads JSON-RPC from stdin, writes to stdout. What Claude
    // Desktop / Code CLI / Cursor spawn you as.
    auto *transport = new McpStdioServerTransport(&app);

    McpServerConfig cfg;
    cfg.serverInfo   = {"my-server", "0.1.0"};
    cfg.instructions = "Demo server exposing a single echo tool.";

    auto *server = new McpServer(transport, cfg, &app);
    server->addTool(new EchoTool(server));

    server->start();
    return app.exec();
}
```

That's a complete, shippable MCP server. Drop the `.exe` (or the
compiled binary on Linux/Mac) into your host's MCP config and it will
auto-spawn on launch.

### Register with Claude Desktop / Cursor / similar

Most stdio hosts read a JSON config file. The entry for the server
above is:

```json
{
  "mcpServers": {
    "my-server": {
      "command": "C:/path/to/my-server.exe",
      "args": [],
      "env": {}
    }
  }
}
```

### Register with `claude mcp add`

For Claude Code CLI (which this repo was tested against during
development):

```bash
claude mcp add my-server C:/path/to/my-server.exe
```

### Launch from inside another LLMCore app

See `example/mcp-servers.json` and `ChatController::loadMcpConfig()` —
the chat example ships a tiny JSON config loader that spawns the
`example-mcp-server.exe` binary automatically on startup.

---

## 2. Writing tools: the `BaseTool` contract

Every tool is a `QObject` subclass of `BaseTool` that provides:

- a **machine-readable id** (must be unique within the server)
- a **human-readable display name** (surfaced as `title` in MCP 2025-11-25)
- a **description** — natural-language prompt that the LLM reads; be specific
- a **JSON Schema** describing the parameters
- an **async execution entry point** that returns a `QFuture<ToolResult>`

A minimal "no parameters, text output" tool is only a few lines:

```cpp
class DateTimeTool : public BaseTool
{
    Q_OBJECT
public:
    explicit DateTimeTool(QObject *parent = nullptr) : BaseTool(parent) {}

    QString id()          const override { return "get_datetime"; }
    QString displayName() const override { return "Date & Time"; }
    QString description() const override {
        return "Returns the current date and time. No parameters required.";
    }

    QJsonObject parametersSchema() const override {
        return QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}};
    }

    QFuture<ToolResult> executeAsync(const QJsonObject &) override {
        return QtConcurrent::run([]() -> ToolResult {
            return ToolResult::text(
                QDateTime::currentDateTime().toString(Qt::ISODate));
        });
    }
};
```

Two slightly subtle points:

- **Never block the Qt event loop inside execution.** Wrap the work
  in `QtConcurrent::run(...)` (or issue an async HTTP request and
  return its future). The MCP server runs on the main thread;
  blocking there also blocks the transport.
- **Throwing from inside the future is equivalent to returning an
  error result.** The server catches `McpRemoteError` separately (it
  serialises the numeric code + data verbatim to the JSON-RPC error
  envelope) and falls back to `isError: true` content for anything
  else.

### Parameter validation

LLMCore does not validate input against the declared schema —
validation is the tool's job, and the recommended shape is
"decode, sanity-check, return an error result on bad input", not
"throw". The model sees the error message and typically self-corrects
on the next turn.

```cpp
QFuture<ToolResult> executeAsync(const QJsonObject &input) override
{
    return QtConcurrent::run([input]() -> ToolResult {
        const double a = input.value("a").toDouble();
        const double b = input.value("b").toDouble();
        const QString op = input.value("operation").toString();

        if (op == "divide" && b == 0.0)
            return ToolResult::error("division by zero");

        double result = 0.0;
        if      (op == "add")      result = a + b;
        else if (op == "subtract") result = a - b;
        else if (op == "multiply") result = a * b;
        else if (op == "divide")   result = a / b;
        else return ToolResult::error(QString("unknown operation '%1'").arg(op));

        return ToolResult::text(QString::number(result, 'g', 10));
    });
}
```

### Rich content: image / audio / resource blocks

The default `ToolResult::text("...")` / `ToolResult::error("...")`
factories are enough for 95 % of tools, but a tool can return a list
of content blocks of mixed types. This is preserved end-to-end: on the
client side, a `ClaudeClient` / `OpenAIResponsesClient` /
`GoogleAIClient` will feed the image straight to the model as part
of the next continuation.

```cpp
#include <QFile>
#include <QMimeDatabase>

class ReadImageTool : public BaseTool
{
    Q_OBJECT
public:
    explicit ReadImageTool(QObject *parent = nullptr) : BaseTool(parent) {}

    QString id()          const override { return "read_image"; }
    QString displayName() const override { return "Read Image"; }
    QString description() const override {
        return "Reads an image file by absolute path and returns it as "
               "a base64 image block. Parameters: path (string).";
    }
    QJsonObject parametersSchema() const override {
        return QJsonObject{
            {"type", "object"},
            {"properties",
             QJsonObject{{"path", QJsonObject{{"type", "string"}}}}},
            {"required", QJsonArray{"path"}},
        };
    }

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> ToolResult {
            const QString path = input.value("path").toString();
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                return ToolResult::error(QString("cannot open %1").arg(path));

            const QByteArray bytes = f.readAll();
            const QString mime = QMimeDatabase()
                                     .mimeTypeForFile(QFileInfo(path))
                                     .name();
            if (!mime.startsWith("image/"))
                return ToolResult::error(QString("not an image: %1").arg(mime));

            ToolResult r;
            r.content.append(ToolContent::makeText(
                QString("Loaded %1 (%2 bytes)").arg(path).arg(bytes.size())));
            r.content.append(ToolContent::makeImage(bytes, mime));
            return r;
        });
    }
};
```

Other factories available on `ToolContent`:

- `makeText(text)`
- `makeImage(bytes, mimeType)`
- `makeAudio(bytes, mimeType)` — MCP 2025-03-26+
- `makeResourceText(uri, text, mimeType)` / `makeResourceBlob(uri, blob, mimeType)`
  — embed a full resource body inside the tool result
- `makeResourceLink(uri, name, description, mimeType)` —
  MCP 2025-06-18+; reference a resource by URI without embedding it

For MCP 2025-06-18 `structuredContent`, fill `ToolResult::structuredContent`
with whatever JSON object matches your declared `outputSchema`.

### Progress and cancellation inside a tool

If the client issued the request with a progress token, the server
side captures it on the current handler's dispatch context. The tool
can publish progress updates and check for cancellation through the
session:

```cpp
class ProgressiveTool : public BaseTool
{
    Q_OBJECT
public:
    ProgressiveTool(McpServer *server, QObject *parent = nullptr)
        : BaseTool(parent), m_server(server) {}

    // id / displayName / description / parametersSchema elided...

    QFuture<ToolResult> executeAsync(const QJsonObject &input) override
    {
        const int steps = input.value("steps").toInt(10);

        // Capture the progress token synchronously — it is only valid
        // while the request handler is running on the main thread.
        const QString token = m_server->session()->currentProgressToken();
        McpSession *session = m_server->session();

        return QtConcurrent::run([steps, token, session, this]() -> ToolResult {
            for (int i = 1; i <= steps; ++i) {
                // Periodically check for a client-issued cancel.
                if (session->isRequestCancelled(token))
                    throw McpCancelledError();

                // Do some work...
                QThread::msleep(100);

                // Publish progress; server-side helpers marshal the
                // notification back to the main thread automatically.
                QMetaObject::invokeMethod(session,
                    [session, token, i, steps]() {
                        session->sendProgress(token, i, steps,
                            QString("step %1 of %2").arg(i).arg(steps));
                    },
                    Qt::QueuedConnection);
            }
            return ToolResult::text("done");
        });
    }

private:
    McpServer *m_server;
};
```

Throwing `McpCancelledError` from inside the future is mapped to
JSON-RPC error code `-32800 (RequestCancelled)` by the session
layer; the client sees the same on `McpClient::callToolWithProgress`
and its future completes with an `McpCancelledError` exception.

---

## 3. Publishing resources

Resources are the MCP equivalent of "here are some files / records /
URLs I can hand you". Subclass `BaseResourceProvider` and register it:

```cpp
#include <LLMCore/BaseResourceProvider.hpp>

class InMemoryResources : public BaseResourceProvider
{
    Q_OBJECT
public:
    void add(const QString &uri, const QString &body) { m_pages[uri] = body; }

    QFuture<QList<ResourceInfo>> listResources() override
    {
        QList<ResourceInfo> out;
        for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
            ResourceInfo r;
            r.uri      = it.key();
            r.name     = QFileInfo(it.key()).fileName();
            r.mimeType = "text/plain";
            out.append(r);
        }
        auto p = std::make_shared<QPromise<QList<ResourceInfo>>>();
        p->start(); p->addResult(out); p->finish();
        return p->future();
    }

    QFuture<ResourceContents> readResource(const QString &uri) override
    {
        auto p = std::make_shared<QPromise<ResourceContents>>();
        p->start();
        if (m_pages.contains(uri)) {
            ResourceContents c;
            c.uri      = uri;
            c.mimeType = "text/plain";
            c.text     = m_pages.value(uri);
            p->addResult(c);
        } else {
            // "I don't own this URI" — return an empty ResourceContents
            // sentinel. The server will walk the next provider.
            p->addResult(ResourceContents{});
        }
        p->finish();
        return p->future();
    }

private:
    QHash<QString, QString> m_pages;
};
```

Wire it up:

```cpp
auto *resources = new InMemoryResources(&app);
resources->add("mem://greeting", "hello");
resources->add("mem://motd", "be excellent to each other");
server->addResourceProvider(resources);
```

The server advertises `resources.listChanged: true` once you register
at least one provider. When your collection changes, signal it from
the provider — the server forwards it as
`notifications/resources/list_changed`. Per-resource change
notifications are forwarded to subscribed clients the same way.

### URI templates

If your resources are dynamically generated from a URL pattern,
override `listResourceTemplates()` so clients can autocomplete them
via `completion/complete`:

```cpp
QFuture<QList<ResourceTemplate>> listResourceTemplates() override
{
    ResourceTemplate t;
    t.uriTemplate = "file:///logs/{date}.log";
    t.name        = "Daily log";
    t.mimeType    = "text/plain";

    auto p = std::make_shared<QPromise<QList<ResourceTemplate>>>();
    p->start(); p->addResult({t}); p->finish();
    return p->future();
}

// And, optionally, expose suggestions for the {date} placeholder:
QFuture<CompletionResult> completeArgument(
    const QString &templateUri,
    const QString &placeholderName,
    const QString &partialValue,
    const QJsonObject &) override
{
    CompletionResult r;
    if (templateUri == "file:///logs/{date}.log" && placeholderName == "date") {
        for (const QString &day : {"2026-04-01", "2026-04-10", "2026-04-11"})
            if (day.startsWith(partialValue))
                r.values.append(day);
    }
    auto p = std::make_shared<QPromise<CompletionResult>>();
    p->start(); p->addResult(r); p->finish();
    return p->future();
}
```

### Subscriptions

Override the subscription support flag and the subscribe/unsubscribe
hooks to opt into per-resource change pushes. When a resource updates,
signal from the provider and the server forwards
`notifications/resources/updated` to every subscribed client.

---

## 4. Publishing prompt templates

Prompts are reusable, parameterised messages. Subclass
`BasePromptProvider`:

```cpp
#include <LLMCore/BasePromptProvider.hpp>

class GreetingPromptProvider : public BasePromptProvider
{
    Q_OBJECT
public:
    QFuture<QList<PromptInfo>> listPrompts() override
    {
        PromptInfo info;
        info.name        = "greet";
        info.description = "Generates a greeting addressed to `name`.";

        PromptArgument arg;
        arg.name        = "name";
        arg.description = "Who to greet";
        arg.required    = true;
        info.arguments  = {arg};

        auto p = std::make_shared<QPromise<QList<PromptInfo>>>();
        p->start(); p->addResult({info}); p->finish();
        return p->future();
    }

    QFuture<PromptGetResult> getPrompt(
        const QString &name, const QJsonObject &args) override
    {
        auto p = std::make_shared<QPromise<PromptGetResult>>();
        p->start();

        if (name != "greet") {
            // Report missing prompt / bad arguments by throwing from
            // inside the future — the server serialises it to the
            // JSON-RPC error envelope with InvalidParams.
            p->setException(std::make_exception_ptr(McpRemoteError(
                ErrorCode::InvalidParams,
                QString("Unknown prompt: %1").arg(name))));
            p->finish();
            return p->future();
        }

        const QString who = args.value("name").toString("world");

        PromptGetResult r;
        r.description = "Greeting";
        PromptMessage m;
        m.role    = "user";
        m.content = QJsonObject{
            {"type", "text"},
            {"text", QString("Hello, %1!").arg(who)},
        };
        r.messages.append(m);
        p->addResult(r);
        p->finish();
        return p->future();
    }
};
```

Register the provider with the server and signal from it when the set
of available prompts changes.

Optionally override the argument completion hook to feed
`completion/complete` — advisory, returns an empty list by default.

---

## 5. Logging (server → client)

The server can push log lines to the connected client via
`notifications/message`. By default the logging capability is
advertised. To send a log line:

```cpp
server->sendLogMessage(
    /*level=*/ LogLevel::Warning,
    /*logger=*/ "my-server.tools.echo",
    /*data=*/   QJsonObject{{"input_length", 42}},
    /*message=*/ "truncating payload to 10 KiB");
```

Honour the client's requested minimum level (defaults to `"info"`)
before emitting verbose logs. The client sets this via
`logging/setLevel`; the server stores it and notifies you of changes
so you can react.

> Note: this is the application-level log channel. LLMCore's own
> library-internal logs still go through Qt's
> `QLoggingCategory` system — enable them with
> `QT_LOGGING_RULES="llmcore.mcp*=true"`.

---

## 6. Hosting over HTTP

LLMCore ships a server-side HTTP transport,
`McpHttpServerTransport`, that speaks the 2025-03-26 Streamable HTTP
wire format on top of a hand-rolled `QTcpServer`-based HTTP/1.1
parser. No dependency on the optional `Qt6::HttpServer` module —
`Qt6::Network` is enough.

```cpp
#include <LLMCore/McpHttpServerTransport.hpp>

HttpServerConfig serverCfg;
serverCfg.address = QHostAddress::LocalHost; // loopback by default!
serverCfg.port    = 0;          // 0 = let the OS pick a port
serverCfg.path    = "/mcp";     // served path; everything else -> 404
// Optional: restrict the Origin header allow-list (2025-11-25 requirement
// for non-loopback deployments). Empty list = accept any Origin.
serverCfg.allowedOrigins = {"https://my-app.example.com"};

auto *transport = new McpHttpServerTransport(serverCfg, &app);

McpServerConfig cfg;
cfg.serverInfo = {"my-http-server", "0.1.0"};

auto *server = new McpServer(transport, cfg, &app);
server->addTool(new EchoTool(server));

server->start();

// Now find out which port the OS actually picked and print the URL.
const quint16 port = transport->serverPort();
qInfo().noquote() << "MCP server listening on http://127.0.0.1:"
                  << port << serverCfg.path;
```

A client can then consume it with the normal `McpHttpTransport`:

```cpp
HttpTransportConfig clientCfg;
clientCfg.endpoint = QUrl(QString("http://127.0.0.1:%1/mcp").arg(port));
clientCfg.spec     = McpHttpSpec::V2025_03_26;

auto *clientTransport = new McpHttpTransport(clientCfg, &app);
McpClient client(clientTransport);
client.connectAndInitialize();
```

That exact pattern is the `tst_McpHttpServer.HandshakeAndToolCallOverHttp`
loopback test — copy-paste from there for a runnable starting point.

### Security notes

- **Bind to loopback** unless you really mean to expose the server to
  the network. The default is `QHostAddress::LocalHost`; only change
  it if you understand the blast radius.
- **Set `allowedOrigins`** for any non-loopback deployment. The
  2025-11-25 MCP spec requires servers to reject POSTs whose `Origin`
  header is not in an allow-list; LLMCore enforces this when the list
  is non-empty.
- **No auth built in.** Anything that reaches the listen socket can
  issue JSON-RPC calls. Put a reverse proxy / bearer-token gateway
  in front of the server if you need auth; the library does not
  implement OAuth (see `mcp_protocol_coverage.md` §1 Authorization).
- **`Mcp-Session-Id`** is generated as a random UUIDv4 on `start()`
  and echoed back on every response. Inbound POSTs carrying a
  different session id are rejected with HTTP 400. This matches the
  single-session model the rest of the library assumes — if you need
  multi-session fan-out, run one transport per client.

### Known limitation: no long-lived GET stream

The transport does not implement the optional
`GET /mcp` long-lived SSE push channel (explicitly out of scope for
v1 — see `mcp_protocol_coverage.md` §2). Server-initiated traffic
(notifications and server→client requests like `sampling/createMessage`)
is **buffered and flushed on the next inbound POST's response** as an
SSE body. That is enough to support sampling round-trips (the client
POSTs something, the buffered request piggybacks onto the response,
the client's reply arrives as a new POST), but insufficient for truly
spontaneous notifications while no POST is in flight. In practice,
purely client-driven workloads are unaffected.

---

## 7. Bridging the server to an existing `ToolsManager`

If your host application already has a `ToolsManager` full of tools
that its `BaseClient` uses, you can expose the exact same tools over
MCP with one call:

```cpp
#include <LLMCore/ToolsManager.hpp>

ToolsManager tools(ToolSchemaFormat::Claude);
tools.addTool(new DateTimeTool);
tools.addTool(new CalculatorTool);

// Server shares the same tool set.
auto *transport = new McpStdioServerTransport(&app);
auto *server    = new McpServer(transport, McpServerConfig{}, &app);
server->setToolsManager(&tools);
server->start();
```

You can mix-and-match: registering a standalone `BaseTool` on the
server only (the ownership contract is looser — the server does NOT
reparent), while sharing a `ToolsManager` exposes everything that
manager already owns. Standalone and manager-backed tools merge into
a single `tools/list` response, sorted alphabetically by id.

Removing a tool pushes `notifications/tools/list_changed` if the
server is already initialized, so connected clients re-sync their
registered tool set automatically.

---

## 8. Embedding server + client in one process

Useful for tests, side-car services, or "fan out a single tool set to
an in-process MCP client plus an external one": the `McpPipeTransport`
gives you a paired set of transports that deliver messages through
queued signal/slot connections.

```cpp
#include <LLMCore/McpPipeTransport.hpp>

auto [serverTransport, clientTransport] = McpPipeTransport::createPair();

// Server side
McpServer server(serverTransport, McpServerConfig{});
server.addTool(new EchoTool(&server));

// Client side — same process, same thread, same event loop
McpClient client(clientTransport);

server.start();
client.connectAndInitialize()
    .then(&app, [&client]() { return client.callTool("echo", {{"text","hi"}}); })
    .unwrap()
    .then(&app, [](const ToolResult &r) { qInfo() << r.asText(); });
```

Every `tst_McpLoopback.cpp` test is built on this pattern.

---

## 9. Debugging tips

- **Enable category logging** to see every JSON-RPC message move
  across the session:
  ```
  QT_LOGGING_RULES="llmcore.mcp*=true" ./my-server
  ```
- **Trace stdio wire traffic** by setting `LLMCORE_MCP_TRACE` to an
  absolute file path. `McpStdioServerTransport` will append every
  received and sent line to that file, which is invaluable when a
  host like Claude Desktop is launching your server and you cannot
  attach a debugger.
- **Smoke-test over HTTP** with `example-mcp-http-probe.exe`:
  ```
  mcp-http-probe --spec 2025-03-26 http://127.0.0.1:8080/mcp echo
  ```
  It runs the initialize handshake, lists tools, optionally calls
  one, and exits with a clear error if anything goes wrong.
- **Return `ToolResult::error(msg)`**, don't throw, when a tool
  "failed for this input". The model sees `isError: true` + your
  message in the next turn and can self-correct. Throwing is still
  fine — it's functionally equivalent — but error-as-value is
  easier to reason about.

---

## See also

- [`client-usage.md`](client-usage.md) — consuming MCP servers from an
  LLMCore app (the mirror image of this document).
- [`sampling.md`](sampling.md) — `sampling/createMessage` — how the
  server asks the client to run a prompt through its LLM.
- [`architecture.md`](architecture.md) — the full library architecture,
  including the class diagram, the sequence diagram for a tool-call
  round-trip, and the async chain model.
- `example/mcp-server-demo/main.cpp` — the runnable example that ships
  with the repo. Builds to `example-mcp-server.exe` and is used by
  `example-chat.exe` out of the box.
- `example/ExampleTools.hpp` — ready-to-copy `BaseTool` subclasses
  (date/time, calculator, IPv4 listing, env lookup, image reader,
  system info).
- `tests/tst_McpLoopback.cpp` and `tests/tst_McpHttpServer.cpp` —
  exhaustive reference for every server-side capability over pipe,
  stdio, and HTTP transports.
