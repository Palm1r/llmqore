# Quick Start

## Minimal example

```cpp
#include <LLMCore/Clients>

auto *client = new LLMCore::ClaudeClient(
    "https://api.anthropic.com", "sk-...", "claude-sonnet-4-20250514", this);

LLMCore::RequestCallbacks cb;
cb.onChunk = [](const LLMCore::RequestID &, const QString &chunk) {
    qDebug() << chunk;
};
cb.onCompleted = [](const LLMCore::RequestID &, const QString &full) {
    qDebug() << "Done:" << full;
};
cb.onFailed = [](const LLMCore::RequestID &, const QString &err) {
    qWarning() << "Error:" << err;
};

client->ask("What is Qt?", cb);
```

## Full payload control

```cpp
QJsonObject payload;
payload["model"] = "claude-sonnet-4-20250514";
payload["max_tokens"] = 4096;
payload["stream"] = true;
payload["messages"] = QJsonArray{
    QJsonObject{{"role", "user"}, {"content", "Explain RAII in C++"}}
};

client->sendMessage(payload, cb);
```

## Using signals instead of callbacks

```cpp
auto *client = new LLMCore::OpenAIClient(url, apiKey, model, this);

connect(client, &LLMCore::BaseClient::chunkReceived,
        this, [](const LLMCore::RequestID &, const QString &chunk) {
    qDebug() << chunk;
});

connect(client, &LLMCore::BaseClient::requestCompleted,
        this, [](const LLMCore::RequestID &, const QString &full) {
    qDebug() << "Done:" << full;
});

client->ask("Hello!");
```

## Thinking / reasoning blocks

```cpp
LLMCore::RequestCallbacks cb;
cb.onThinkingBlock = [](const LLMCore::RequestID &,
                         const QString &thinking,
                         const QString &signature) {
    qDebug() << "Thinking:" << thinking.left(200) << "...";
};
```

## Cancel a request

```cpp
LLMCore::RequestID id = client->ask("Write a long essay...", cb);
// ...later:
client->cancelRequest(id);
```

## MCP Server

```cpp
#include <LLMCore/Mcp>

// Create stdio transport (reads stdin, writes stdout)
auto *transport = new LLMCore::McpStdioServerTransport(&app);

// Configure and create server
LLMCore::McpServerConfig cfg;
cfg.serverInfo = {"my-server", "1.0.0"};
cfg.instructions = "My MCP server with custom tools";

auto *server = new LLMCore::McpServer(transport, cfg, &app);

// Register tools
server->addTool(new MyCustomTool(server));

server->start();
```

### HTTP transport

```cpp
LLMCore::HttpServerConfig httpCfg;
httpCfg.port = 8080;
httpCfg.path = "/mcp";

auto *transport = new LLMCore::McpHttpServerTransport(httpCfg, &app);
auto *server = new LLMCore::McpServer(transport, cfg, &app);
server->start();
```

## MCP Client

```cpp
#include <LLMCore/Mcp>

// Launch an MCP server as a subprocess
LLMCore::StdioLaunchConfig launch;
launch.program = "my-mcp-server";
launch.arguments = {"--verbose"};

auto *transport = new LLMCore::McpStdioClientTransport(launch, &app);
auto *client = new LLMCore::McpClient(transport,
    LLMCore::Implementation{"my-app", "1.0.0"}, &app);

// Connect and initialize
auto future = client->connectAndInitialize(std::chrono::seconds(10));
auto *watcher = new QFutureWatcher<LLMCore::InitializeResult>(&app);

connect(watcher, &QFutureWatcher<LLMCore::InitializeResult>::finished,
        &app, [=]() {
    auto result = watcher->result();
    qInfo() << "Connected to:" << result.serverInfo.name;

    // List available tools
    client->listTools().then([](QList<LLMCore::ToolInfo> tools) {
        for (const auto &tool : tools)
            qDebug() << tool.name << "-" << tool.description;
    });

    // Call a tool
    client->callTool("get_datetime", {}).then([](LLMCore::ToolResult r) {
        qDebug() << "Result:" << r.text();
    });
});

watcher->setFuture(future);
```

### Bind MCP tools into LLM clients

```cpp
// Register all MCP server tools into the LLM client's ToolsManager.
// Automatically refreshes when the server's tool list changes.
claudeClient->tools()->addMcpClient(mcpClient);

// One MCP server can serve multiple LLM clients
openaiClient->tools()->addMcpClient(mcpClient);

// Both clients can now use MCP tools in conversations
claudeClient->ask("What time is it?", cb);
```

### Connect via HTTP

```cpp
LLMCore::HttpTransportConfig httpCfg;
httpCfg.endpoint = QUrl("http://localhost:8080/mcp");

auto *transport = new LLMCore::McpHttpTransport(httpCfg, nullptr, &app);
auto *client = new LLMCore::McpClient(transport, {"my-app", "1.0.0"}, &app);
```