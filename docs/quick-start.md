# Quick Start

## Thread contract (read this first)

All LLMQore client objects (`BaseClient` and subclasses, `ToolsManager`,
`ToolRegistry`, `ToolHandler`, `McpClient`) live on the thread of the
`QObject` parent passed to their constructor. All public methods must be
called from that thread; all signals are emitted on that thread.

Cross-thread consumers should connect to signals with `Qt::AutoConnection`
(the default) — Qt will queue delivery and copy arguments safely. Do not
hold `BaseTool *` pointers returned from `ToolRegistry::registeredTools()`
across event-loop iterations; use `toolsSnapshot()` instead if you need
to pass tool metadata to another thread or widget.

Debug builds enforce the contract with `Q_ASSERT_X` on every mutating or
raw-pointer-returning method.

## LLM Clients

### Minimal example

```cpp
#include <LLMQore/Clients>

auto *client = new LLMQore::ClaudeClient(
    "https://api.anthropic.com", "sk-...", "claude-sonnet-4-20250514", this);

connect(client, &LLMQore::BaseClient::chunkReceived,
        this, [](const LLMQore::RequestID &, const QString &chunk) {
    qDebug() << chunk;
});
connect(client, &LLMQore::BaseClient::requestCompleted,
        this, [](const LLMQore::RequestID &, const QString &full) {
    qDebug() << "Done:" << full;
});
connect(client, &LLMQore::BaseClient::requestFailed,
        this, [](const LLMQore::RequestID &, const QString &err) {
    qWarning() << "Error:" << err;
});

client->ask("What is Qt?");
```

### Full payload control

```cpp
QJsonObject payload;
payload["model"] = "claude-sonnet-4-20250514";
payload["max_tokens"] = 4096;
payload["stream"] = true;
payload["messages"] = QJsonArray{
    QJsonObject{{"role", "user"}, {"content", "Explain RAII in C++"}}
};

client->sendMessage(payload);
```

### Rich completion metadata

`requestFinalized` fires alongside `requestCompleted` with a
`CompletionInfo` struct containing `fullText`, `model` and `stopReason`:

```cpp
connect(client, &LLMQore::BaseClient::requestFinalized,
        this, [](const LLMQore::RequestID &, const LLMQore::CompletionInfo &info) {
    qDebug() << "model:" << info.model << "stopReason:" << info.stopReason;
});
```

`requestFinalized` is emitted BEFORE `requestCompleted` so consumers
resolving a `QPromise` on finalization run before any simple-text handler.

### Thinking / reasoning blocks

```cpp
connect(client, &LLMQore::BaseClient::thinkingBlockReceived,
        this, [](const LLMQore::RequestID &,
                 const QString &thinking,
                 const QString &signature) {
    qDebug() << "Thinking:" << thinking.left(200) << "...";
});
```

### Cancel a request

```cpp
LLMQore::RequestID id = client->ask("Write a long essay...");
// ...later:
client->cancelRequest(id);
```

## Tools

### Define a tool

Subclass `BaseTool` to create a tool that LLM can call:

```cpp
#include <LLMQore/BaseTool.hpp>
#include <QtConcurrent>

class GetWeatherTool : public LLMQore::BaseTool
{
    Q_OBJECT
public:
    using BaseTool::BaseTool;

    QString id() const override { return "get_weather"; }
    QString displayName() const override { return "Get Weather"; }
    QString description() const override { return "Returns current weather for a city."; }

    QJsonObject parametersSchema() const override
    {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"city", QJsonObject{{"type", "string"}, {"description", "City name"}}},
            }},
            {"required", QJsonArray{"city"}}
        };
    }

    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        return QtConcurrent::run([input]() -> LLMQore::ToolResult {
            QString city = input["city"].toString();
            // ... fetch real weather data ...
            return LLMQore::ToolResult::text(QString("22°C, sunny in %1").arg(city));
        });
    }
};
```

### Register and use with an LLM client

```cpp
client->tools()->addTool(new GetWeatherTool(client));
client->ask("What's the weather in Berlin?");
```

### Inspect the registered tool list for UI

Use `toolsSnapshot()` for anything that outlives a single function call
(e.g. displaying the tool list in a UI). It returns a detached copy that
cannot dangle if a tool is later removed. Do NOT hold `BaseTool *`
pointers from `registeredTools()` beyond the immediate call.

```cpp
for (const auto &snap : client->tools()->toolsSnapshot()) {
    ui->addRow(snap.displayName, snap.description);
}
```

The tool works the same way whether registered directly or exposed through an MCP server.

### Register in an MCP server

```cpp
server->addTool(new GetWeatherTool(server));
```

Now any MCP client connecting to this server will see and can call `get_weather`.

## MCP Server

### stdio transport

For tools that integrate with Claude Desktop, Cursor, VS Code, etc.:

```cpp
#include <LLMQore/Mcp>

auto *transport = new LLMQore::McpStdioServerTransport(&app);

LLMQore::McpServerConfig cfg;
cfg.serverInfo = {"my-server", "1.0.0"};
cfg.instructions = "My MCP server with custom tools";

auto *server = new LLMQore::McpServer(transport, cfg, &app);
server->addTool(new MyCustomTool(server));
server->start();
```

### Streamable HTTP transport

For remote or multi-client access:

```cpp
#include <LLMQore/Mcp>

LLMQore::HttpServerConfig httpCfg;
httpCfg.port = 8080;
httpCfg.path = "/mcp";

auto *transport = new LLMQore::McpHttpServerTransport(httpCfg, &app);

LLMQore::McpServerConfig cfg;
cfg.serverInfo = {"my-server", "1.0.0"};

auto *server = new LLMQore::McpServer(transport, cfg, &app);
server->addTool(new MyCustomTool(server));
server->start();
```

## MCP Client

### Add servers programmatically

```cpp
// stdio — launch an MCP server as a subprocess
client->tools()->addMcpServer({
    .name = "filesystem",
    .command = "npx",
    .arguments = {"-y", "@modelcontextprotocol/server-filesystem", "/home/user"}
});

// Streamable HTTP — connect to a remote MCP server
client->tools()->addMcpServer({
    .name = "remote-tools",
    .url = QUrl("http://localhost:8080/mcp")
});
```

### Load from a JSON config

```cpp
QFile file("mcp_servers.json");
file.open(QIODevice::ReadOnly);
client->tools()->loadMcpServers(QJsonDocument::fromJson(file.readAll()).object());
```

Config format (compatible with Claude Desktop):

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"]
    },
    "database": {
      "command": "uvx",
      "args": ["mcp-server-sqlite", "--db-path", "/path/to/db.sqlite"]
    },
    "remote": {
      "url": "http://localhost:8080/mcp",
      "headers": {
        "Authorization": "Bearer token"
      }
    }
  }
}
```

### Share one MCP server across multiple LLM providers

```cpp
auto *mcpClient = new LLMQore::McpClient(transport, {"my-app", "1.0.0"}, &app);
mcpClient->connectAndInitialize();

claudeClient->tools()->addMcpClient(mcpClient);
openaiClient->tools()->addMcpClient(mcpClient);
```

### Use the MCP client directly

```cpp
auto *transport = new LLMQore::McpStdioClientTransport(
    {.program = "my-mcp-server", .arguments = {"--verbose"}}, &app);
auto *mcpClient = new LLMQore::McpClient(transport, {"my-app", "1.0.0"}, &app);

mcpClient->connectAndInitialize().then([mcpClient]() {
    // List available tools
    mcpClient->listTools().then([](QList<LLMQore::ToolInfo> tools) {
        for (const auto &tool : tools)
            qDebug() << tool.name << "-" << tool.description;
    });

    // Call a tool directly
    mcpClient->callTool("get_datetime", {}).then([](LLMQore::ToolResult r) {
        qDebug() << "Result:" << r.asText();
    });

    // List resources
    mcpClient->listResources().then([](QList<LLMQore::ResourceInfo> resources) {
        for (const auto &r : resources)
            qDebug() << r.uri << "-" << r.name;
    });
});
```
