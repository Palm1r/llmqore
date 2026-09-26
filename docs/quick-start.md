# Quick Start

A working console program in six steps, then the same client wired into QML. Every snippet
is complete — nothing is elided.

For the full reference of what a client can do, see [LLM clients](llm-clients.md). Read
[the thread contract](threading.md) before touching a client from more than one thread.

## 1. Build against LLMQore

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)
project(hello-llm LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)

include(FetchContent)
FetchContent_Declare(
    LLMQore
    GIT_REPOSITORY https://github.com/palm1r/llmqore.git
    GIT_TAG v0.8.0
)
FetchContent_MakeAvailable(LLMQore)

add_executable(hello-llm main.cpp)
set_target_properties(hello-llm PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(hello-llm PRIVATE LLMQore::LLMQore Qt6::Core)
```

The library needs `Core`, `Network` and `Concurrent` — no GUI module.

## 2. Stream an answer

`main.cpp`:

```cpp
#include <QCoreApplication>
#include <QTextStream>

#include <LLMQore/Clients>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    auto *client = new LLMQore::ClaudeClient(
        "https://api.anthropic.com",
        qEnvironmentVariable("CLAUDE_API_KEY"),
        "claude-sonnet-4-5",
        &app);

    QObject::connect(client, &LLMQore::BaseClient::chunkReceived,
                     &app, [](const LLMQore::RequestID &, const QString &chunk) {
        QTextStream(stdout) << chunk << Qt::flush;
    });

    QObject::connect(client, &LLMQore::BaseClient::requestCompleted,
                     &app, [](const LLMQore::RequestID &, const QString &) {
        QCoreApplication::quit();
    });

    QObject::connect(client, &LLMQore::BaseClient::requestFailed,
                     &app, [](const LLMQore::RequestID &, const QString &error) {
        QTextStream(stderr) << error << Qt::endl;
        QCoreApplication::exit(1);
    });

    client->ask("What is Qt?");
    return app.exec();
}
```

```bash
export CLAUDE_API_KEY=sk-...
cmake -B build && cmake --build build && ./build/hello-llm
```

Tokens arrive as they are generated. `chunkReceived` is emitted on the client's own thread,
so in a GUI the same `connect` writes straight into a widget or a model — no marshalling, no
worker thread. Its sibling `accumulatedReceived` carries the whole answer so far, which is
usually what a text view wants.

`ask()` returns a `RequestID`. Pass it to `cancelRequest()` to stop a long answer.

When you want one answer rather than a stream, `askOnce()` gives you a future instead of
three connections:

```cpp
client->askOnce("What is Qt?")
    .then(&app, [](const LLMQore::CompletionInfo &result) {
        QTextStream(stdout) << result.fullText << Qt::endl;
        QCoreApplication::quit();
    })
    .onFailed(&app, [](const std::exception &e) {
        QTextStream(stderr) << e.what() << Qt::endl;
        QCoreApplication::exit(1);
    });
```

Both paths run the same request; the future just settles once, at the end.

## 3. Hold a conversation

`ask(QString)` is a one-turn conversation, so consecutive calls do not accumulate --
`askOnce()` hands the pair back, but the next question starts fresh. For a dialogue, keep a `Conversation` and hand it back each
turn:

```cpp
LLMQore::Conversation conversation;
conversation.setSystem("Answer in one sentence.");
conversation.addUser("What is Qt?");

client->ask(conversation);
```

The application owns the history; the client stays stateless. When the turn ends, the full
history — including everything the model added along the way — arrives in
`requestFinalized`:

```cpp
QObject::connect(client, &LLMQore::BaseClient::requestFinalized,
                 &app, [&conversation](const LLMQore::RequestID &,
                                       const LLMQore::CompletionInfo &info) {
    conversation = info.conversation;
});
```

The next turn is then `conversation.addUser("And QML?"); client->ask(conversation);`.

One `Conversation` works against every provider. `messages` against `contents`, `assistant`
against `model`, a top-level `system` field against a system message inside the array — the
client translates. You never write provider-specific JSON, and you can replay the same
history against a different provider.

Anything the library does not model goes in a second argument, merged into the payload last
so it always wins:

```cpp
client->ask(conversation, {{"temperature", 0.2}});
```

## 4. Let the model call your code

Subclass `BaseTool`:

```cpp
#include <LLMQore/BaseTool.hpp>
#include <LLMQore/Tools>
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
            {"required", QJsonArray{"city"}}};
    }

    QFuture<LLMQore::ToolResult> executeAsync(const QJsonObject &input) override
    {
        const QString city = input["city"].toString();
        return QtConcurrent::run([city]() -> LLMQore::ToolResult {
            return LLMQore::ToolResult::text(QString("22°C, sunny in %1").arg(city));
        });
    }
};
```

`<LLMQore/Tools>` is what brings in `ToolsManager`; `<LLMQore/Clients>` only forward-declares
it. Register the tool and ask:

```cpp
client->tools()->addTool(new GetWeatherTool(client));

conversation.addUser("What's the weather in Berlin?");
client->ask(conversation);
```

The client runs the loop itself: the model asks for the tool, the tool executes, the result
goes back, the model answers. Watch it through `toolStarted` and `toolResultReady`. The
loop is bounded — ten rounds per request by default, `setMaxToolContinuations()` changes it.

## 5. Borrow someone else's tools

An MCP server is a process that offers tools. Point the client at one and its tools join
the same set:

```cpp
client->tools()->addMcpServer({
    .name = "filesystem",
    .command = "npx",
    .arguments = {"-y", "@modelcontextprotocol/server-filesystem", "/home/user"}});
```

Or load a config in the format Claude Desktop uses:

```cpp
QFile file("mcp_servers.json");
file.open(QIODevice::ReadOnly);
client->tools()->loadMcpServers(QJsonDocument::fromJson(file.readAll()).object());
```

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"]
    },
    "remote": {
      "url": "http://localhost:8080/mcp",
      "headers": { "Authorization": "Bearer token" }
    }
  }
}
```

`GetWeatherTool` and the filesystem tools are now indistinguishable to the model.

## 6. Offer your tools to someone else

The same set can point outward. Hand the registry to an MCP server and an external agent —
Claude Desktop, Claude Code, an editor — can call your application's tools while your own
model keeps calling them too:

```cpp
#include <LLMQore/Mcp>

LLMQore::Mcp::HttpServerConfig httpConfig;
httpConfig.port = 8080;
httpConfig.path = "/mcp";

LLMQore::Mcp::McpServerConfig config;
config.serverInfo = {"my-app", "1.0.0"};

auto *server = new LLMQore::Mcp::McpServer(
    new LLMQore::Mcp::McpHttpServerTransport(httpConfig, &app), config, &app);

server->setToolRegistry(client->tools());
server->start();
```

Two lines beyond what you already had. Tools added later show up by themselves — the server
forwards `toolsChanged` as `notifications/tools/list_changed`.

Use **HTTP** here. `McpStdioServerTransport` takes over the process's stdin and stdout,
which is right for a program that is nothing but an MCP server and wrong for one that has a
UI. For the stdio-only case as a ready-made binary, see [MCP Bridge](mcp-bridge.md).

## From console to QML

The client does not change. Wrap it in a controller and expose that:

```cpp
class ChatController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MessageModel *messages READ messages CONSTANT)

public:
    explicit ChatController(QObject *parent = nullptr)
        : QObject(parent)
        , m_client(new LLMQore::ClaudeClient(
              "https://api.anthropic.com", qEnvironmentVariable("CLAUDE_API_KEY"),
              "claude-sonnet-4-5", this))
    {
        connect(m_client, &LLMQore::BaseClient::chunkReceived, this,
                [this](const LLMQore::RequestID &, const QString &chunk) {
            m_messages.appendOrCreate("assistant", chunk);
        });
        connect(m_client, &LLMQore::BaseClient::requestFinalized, this,
                [this](const LLMQore::RequestID &, const LLMQore::CompletionInfo &info) {
            m_conversation = info.conversation;
        });
    }

    MessageModel *messages() { return &m_messages; }

    Q_INVOKABLE void send(const QString &text)
    {
        m_messages.append("user", text);
        m_conversation.addUser(text);
        m_client->ask(m_conversation);
    }

private:
    MessageModel m_messages;
    LLMQore::Conversation m_conversation;
    LLMQore::BaseClient *m_client;
};
```

The part worth copying carefully is the model. A streaming chunk must **extend the last
row**, not append a new one:

```cpp
void appendOrCreate(const QString &role, const QString &chunk)
{
    if (!m_messages.isEmpty() && m_messages.last().role == role) {
        m_messages.last().text += chunk;
        const auto idx = index(m_messages.size() - 1);
        emit dataChanged(idx, idx, {TextRole});
    } else {
        append(role, chunk);
    }
}
```

Get that wrong and every token becomes its own bubble.

```qml
ListView {
    model: controller.messages
    delegate: Text { text: model.text }
}
```

The full application — provider switching, tool badges, an ACP agent — is
[`example-chat`](../example/Main.qml). Build it with `-DLLMQORE_BUILD_EXAMPLES=ON`; it needs
Qt Quick and Qt 6.

<img width="912" alt="example-chat" src="https://github.com/user-attachments/assets/2fb1ea83-1d2d-4016-9c87-56180dbf3301" />
