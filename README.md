# LLMQore

[![Build and Test](https://github.com/Palm1r/llmqore/actions/workflows/build_and_test.yml/badge.svg?branch=main)](https://github.com/Palm1r/llmqore/actions/workflows/build_and_test.yml)
![GitHub Tag](https://img.shields.io/github/v/tag/Palm1r/llmqore)

Qt/C++ library for cloud and local LLM providers, MCP clients and servers, and ACP agents.
Streaming deltas arrive as Qt signals on the object's own thread, async results as
`QFuture`, ownership follows `QObject` parent-child. Links against `Core`, `Network` and
`Concurrent`.

## What it does

- **LLM REST API** — cloud and local: Claude, OpenAI Chat Completions, OpenAI Responses, Google AI, Mistral, DeepSeek, Qwen, Ollama, llama.cpp; streaming or buffered, images in, reasoning out, token usage, cancellation
- **Tool calling** — your `BaseTool` subclass, tool loop included, gated by `QFuture<bool>` if you want
- **MCP** — client and server over stdio, Streamable HTTP or legacy SSE, sharing one tool registry; `mcp-bridge` CLI puts many upstream servers behind one endpoint
- **Conversation** — one history, translated into each provider's shape, portable between them
- **ACP host** — any agent in the JSON registry; ships Claude Code and Codex

## LLM REST API

Ask and await the answer:

```cpp
auto *client = new LLMQore::ClaudeClient(
    "https://api.anthropic.com", apiKey, "claude-sonnet-4-5", this);

client->askOnce("What is Qt?").then(this, [this](const LLMQore::CompletionInfo &result) {
    m_view->setPlainText(result.fullText);
});
```

Or watch it arrive, which is what you want in a chat panel:

```cpp
connect(client, &LLMQore::BaseClient::accumulatedReceived,
        this, [this](const LLMQore::RequestID &, const QString &answer) {
    m_view->setPlainText(answer);
});

client->ask("What is Qt?");
```

`accumulatedReceived` carries the whole answer so far; `chunkReceived` carries only the new
delta. Both are emitted on the client's own thread, so a direct connection into a widget or
model is safe.

→ [Quick Start](docs/quick-start.md)

## Tool calling

Expose a function over data the provider cannot see — the open document, the current
selection, a local database:

```cpp
client->tools()->addTool(new SearchCurrentFileTool(client));

conversation.addUser("Where do we handle the timeout?");
client->ask(conversation);
```

The client drives the loop: the model requests the tool, `executeAsync` runs, the result is
sent back, the model answers. `setMaxToolContinuations()` bounds it, ten rounds by default.
`toolStarted` and `toolResultReady` report progress; `setExecutionGate()` gates each call
behind a `QFuture<bool>`.

→ [Quick Start](docs/quick-start.md) · [LLM clients](docs/llm-clients.md)

## MCP

### Using external tools

Tools from an MCP server enter the same registry and reach the model through the same
tool-definition array:

```cpp
client->tools()->addMcpServer({.name = "filesystem", .command = "npx",
    .arguments = {"-y", "@modelcontextprotocol/server-filesystem", "/home/user"}});

client->tools()->loadMcpServers(QJsonDocument::fromJson(configData).object());
```

`loadMcpServers` reads the `mcpServers` object Claude Desktop uses and returns how many
servers it registered:

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"]
    }
  }
}
```

### Serving your own tools

`McpServer::setToolRegistry` takes the same `ToolsManager` the client uses, so one
registration serves both the in-process model and any MCP client that connects:

```cpp
auto *server = new LLMQore::Mcp::McpServer(
    new LLMQore::Mcp::McpHttpServerTransport({.port = 8080, .path = "/mcp"}, this),
    cfg, this);

server->setToolRegistry(client->tools());
server->start();
```

The registry is shared, not copied: a tool added later reaches both sides, and the server
forwards `toolsChanged` as `notifications/tools/list_changed`.

Use HTTP inside a running application. `McpStdioServerTransport` takes over the process's
stdin and stdout, which only works when the process is nothing but an MCP server.

→ [Quick Start](docs/quick-start.md)

### Bridging transports

`mcp-bridge` is a CLI built on the same client and server. It connects to several upstream
MCP servers and re-exposes their tools behind one HTTP/SSE endpoint or one stdio server,
for when the upstreams and the client disagree on transport.

```bash
mcp-bridge bridge.json              # HTTP endpoint
mcp-bridge --stdio bridge.json      # stdio
```

Prebuilt binaries with the Qt runtime bundled are on
[Releases](https://github.com/Palm1r/llmqore/releases).

→ [MCP Bridge](docs/mcp-bridge.md)

## Local models

`OllamaClient` and `LlamaCppClient` derive from the same `BaseClient` as the hosted
providers and accept an empty API key:

```cpp
auto *client = new LLMQore::OllamaClient("http://localhost:11434", {}, "llama3", this);
```

The conversation, the tools and the signals are the same; only the constructor differs.

→ [Supported providers](#supported-providers)

## Conversation

```cpp
LLMQore::Conversation conversation;
conversation.setSystem("Answer in one sentence.");
conversation.addUser("What is Qt?");

client->ask(conversation);
```

Providers disagree on nearly every name: `messages` against `contents`, `assistant` against
`model`, a top-level `system` field against a system message inside the array. One
`serializeTurn` per provider does the translation, and `CompletionInfo::conversation`
returns the history including the turns the model added during tool rounds.

A turn holds a list of content, so an image is another block in it:

```cpp
conversation.addUser({
    LLMQore::TextContent{"What does this chart show?"},
    LLMQore::ImageContent::fromBytes(png, "image/png")});
```

→ [LLM clients](docs/llm-clients.md)

## ACP host

The reverse direction: the agent owns the model and the tool loop, LLMQore is the host.
It launches Claude Code or Codex over stdio, streams `session/update` as Qt signals, and
answers the agent's `session/request_permission`, `fs/*` and `terminal/*` calls.

```cpp
using namespace LLMQore::Acp;

AcpAgentRegistry registry;
registry.loadFromFile("agents.json");

auto *agent = new AcpClient(
    registry.config("claude", QDir::currentPath())->createTransport(this), {}, this);
agent->setFileSystemProvider(new DefaultFileSystemProvider(this));
agent->setTerminalProvider(new TerminalManager(this));

connect(agent, &AcpClient::agentMessageChunk,
        this, [](const QString &, const ContentBlock &c) { /* render c.text */ });

agent->connectAndInitialize();   // then newSession() -> prompt()
```

`AcpAgentRegistry` reads agents from JSON, overridable with `LLMQORE_ACP_AGENTS`. No API
key travels through the protocol — the agent authenticates itself.

→ [ACP host](docs/acp/architecture.md) · [authentication](docs/acp/authentication.md)

## Example application

<img width="912" alt="example-chat" src="https://github.com/user-attachments/assets/2fb1ea83-1d2d-4016-9c87-56180dbf3301" />

[`example-chat`](example/Main.qml) is a Qt Quick application covering all eight providers,
MCP servers and an ACP agent. Build with `-DLLMQORE_BUILD_EXAMPLES=ON`.

## Supported providers

| Provider | Client class | Streaming | Tools | Images in | Reasoning parsed | Reasoning replayed |
|---|---|---|---|---|---|---|
| Anthropic Claude | `ClaudeClient` | ✓ | ✓ | ✓ | ✓ | ✓ signature |
| OpenAI (Chat Completions) | `OpenAIClient` | ✓ | ✓ | ✓ | ✓ | ✓ when received |
| OpenAI (Responses API) | `OpenAIResponsesClient` | ✓ | ✓ | ✓ | ✓ | opt-in |
| Google AI | `GoogleAIClient` | ✓ | ✓ | ✓ | ✓ | ✓ thought signature |
| Ollama | `OllamaClient` | ✓ | ✓ | ✓ | ✓ | ✓ |
| Mistral | `OpenAIClient` + `mistralProfile()` | ✓ | ✓ | ✓ | ✓ | ✓ when received |
| llama.cpp | `LlamaCppClient` | ✓ | ✓ | ✓ | ✓ | ✓ when received |
| DeepSeek | `OpenAIClient` | ✓ | ✓ | ✓ | ✓ | ✓ when received |
| Qwen (DashScope) | `OpenAIClient` | ✓ | ✓ | ✓ | ✓ | ✓ when received |

**Reasoning parsed** means thinking blocks reach you as signals. **Reasoning replayed**
means they go back into the next request in the form that provider requires — without which
some models reject a continuation that follows a tool call. The Responses API needs
`store: false` to make this work, so it is a switch rather than a default; see
[LLM clients](docs/llm-clients.md).

MCP is implemented for the 2025-11-25 spec over stdio and Streamable HTTP — server side:
tools, resources, resource templates, prompts, completions, sampling, elicitation; client
side: the same plus roots.

## Requirements

- C++20
- Qt 5.15 or Qt 6.5+
- CMake 3.21+

CI builds and tests Qt 6.8.3 and 6.10.2 on Linux, macOS and Windows, and Qt 5.15.2 on
Linux. Versions inside the stated range but outside that matrix are expected to work and
are not verified on every commit. The Qt Quick example is Qt 6 only.

## Documentation

- [Quick Start](docs/quick-start.md) — a complete program, from CMake to QML
- [LLM clients](docs/llm-clients.md) — conversations, models, tools, headers, escape hatches
- [Threading](docs/threading.md) — the thread contract, in full
- [Integration](docs/integration.md) — FetchContent, installed builds, CMake options
- [MCP Bridge](docs/mcp-bridge.md) — aggregate MCP servers behind one endpoint
- [MCP coverage](docs/mcp/mcp_protocol_coverage.md) — spec conformance matrix
- [ACP host](docs/acp/architecture.md) — drive external agents ([as-built notes](docs/acp/implementation-notes.md))
- [Architecture](docs/architecture.md) — internals, for contributors

## Support

- **Report Issues**: [open an issue](https://github.com/Palm1r/llmqore/issues) on GitHub
- **Contribute**: pull requests with bug fixes or new features are welcome
- **Spread the Word**: star the repository and share with fellow developers
- **Financial Support**:
   - Paypal: [paypal page](https://www.paypal.com/paypalme/palm1r)
   - Bitcoin (BTC): `bc1qndq7f0mpnlya48vk7kugvyqj5w89xrg4wzg68t`
   - Ethereum (ETH): `0xA5e8c37c94b24e25F9f1f292a01AF55F03099D8D`
   - Litecoin (LTC): `ltc1qlrxnk30s2pcjchzx4qrxvdjt5gzuervy5mv0vy`
   - USDT (TRC20): `THdZrE7d6epW6ry98GA3MLXRjha1DjKtUx`

## License

MIT — see [LICENSE](LICENSE).
