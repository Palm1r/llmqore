# LLMQore

[![Build and Test](https://github.com/Palm1r/llmqore/actions/workflows/build_and_test.yml/badge.svg?branch=main)](https://github.com/Palm1r/llmqore/actions/workflows/build_and_test.yml)
![GitHub Tag](https://img.shields.io/github/v/tag/Palm1r/llmqore)

Qt/C++ library for working with cloud and local LLM providers, create MCP servers and clients, download and using library MCP Bridge.

**LLM clients** — unified streaming API across all providers:

```cpp
auto *client = new LLMQore::ClaudeClient(url, apiKey, model, this);
client->ask("What is Qt?", cb);
```

**MCP server** — expose tools, resources and prompts over stdio or HTTP:

```cpp
// stdio (stdin/stdout, e.g. for Claude Desktop)
auto *transport = new LLMQore::McpStdioServerTransport(&app);

// or Streamable HTTP
auto *transport = new LLMQore::McpHttpServerTransport({.port = 8080, .path = "/mcp"}, &app);

auto *server = new LLMQore::McpServer(transport, cfg, &app);
server->addTool(new MyTool(server));
server->start();
```

**MCP client** — connect to MCP servers and bind their tools into LLM clients:

```cpp
// Add servers one by one
client->tools()->addMcpServer({.name = "filesystem", .command = "npx",
    .arguments = {"-y", "@modelcontextprotocol/server-filesystem", "/home/user"}});

// Or load from a JSON config
client->tools()->loadMcpServers(QJsonDocument::fromJson(configData).object());
```

`loadMcpServers` accepts:

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
<img width="912" height="740" alt="Screenshot 2026-04-13 at 18 48 18" src="https://github.com/user-attachments/assets/2fb1ea83-1d2d-4016-9c87-56180dbf3301" />

See [Quick Start](docs/quick-start.md) for complete examples.

## MCP Bridge

A standalone CLI tool built on llmqore that aggregates multiple MCP servers (stdio or SSE) and re-exposes them either behind a single HTTP/SSE endpoint or as one stdio server — useful when the upstreams and the client disagree on transport.

```bash
mcp-bridge bridge.json              # HTTP endpoint
mcp-bridge --stdio bridge.json      # stdio (for Claude Desktop and friends)
```

Config uses the familiar `mcpServers` schema:

```json
{
  "port": 8808,
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"]
    }
  }
}
```

Prebuilt binaries for Linux/macOS/Windows (with Qt runtime bundled) are published to [GitHub Releases](https://github.com/Palm1r/llmqore/releases). See [MCP Bridge docs](docs/mcp-bridge.md) for full usage, config reference, and build instructions.

## Supported Providers

| Provider | Client class | Streaming | Tools | Thinking |
|---|---|---|---|---|
| Anthropic Claude | `ClaudeClient` | ✓ | ✓ | ✓ |
| OpenAI (Chat Completions) | `OpenAIClient` | ✓ | ✓ | ✓ |
| OpenAI (Responses API) | `OpenAIResponsesClient` | ✓ | ✓ | ✓ |
| Ollama | `OllamaClient` | ✓ | ✓ | ✓ |
| Google AI | `GoogleAIClient` | ✓ | ✓ | ✓ |
| Mistral | `MistralClient` | ✓ | ✓ | ✓ |
| llama.cpp | `LlamaCppClient` | ✓ | ✓ | ✓ |
| DeepSeek | `OpenAIClient` | ✓ | ✓ | ✓ |

## MCP (Model Context Protocol)

Client and server implementation of the MCP 2025-11-25 spec:

- **Transports**: stdio, Streamable HTTP
- **Server**: tools, resources, resource templates, prompts, completions, sampling, elicitation
- **Client**: tools, resources, prompts, completions, sampling, elicitation, roots

See [MCP Protocol Coverage](docs/mcp/mcp_protocol_coverage.md) for the full spec-conformance matrix.

## ACP (Agent Client Protocol)

Host/client implementation of Zed's Agent Client Protocol — drive an external coding
agent (Claude Code, Gemini CLI, Codex, …) over stdio. LLMQore launches the agent, runs the session, streams its output as Qt
signals, and services the agent's callbacks (permission, file system, terminal).

```cpp
using namespace LLMQore::Acp;

// Agents are data, not code: load a catalogue from JSON (file or :/qrc).
AcpAgentRegistry registry;
registry.loadFromFile("agents.json");
auto cfg = registry.config("claude", QDir::currentPath()).value();

auto *client = new AcpClient(cfg.createTransport(this), {}, this);
client->setFileSystemProvider(new DefaultFileSystemProvider(this));
client->setTerminalProvider(new TerminalManager(this));

connect(client, &AcpClient::agentMessageChunk,
    this, [](const QString &sessionId, const ContentBlock &c){ /* render c.text */ });

client->connectAndInitialize();   // then newSession() -> prompt()
```

```json
// agents.json — the host ships its own catalogue; override with LLMQORE_ACP_AGENTS
{ "agents": [
  { "id": "claude", "name": "Claude Code", "command": "npx",
    "args": ["-y", "@agentclientprotocol/claude-agent-acp"] }
]}
```

**Giving the agent MCP tools** — list MCP servers in `newSession`; the agent connects to
them and runs their tools itself, so you just watch the tool calls stream by (the agent owns
the model + tool loop, you don't register tools on the host):

```cpp
LLMQore::compat(client->connectAndInitialize())
    .then(client, [client](const InitializeResult &) {
        NewSessionParams params;
        params.cwd = QDir::currentPath();
        params.mcpServers = {
            { .name = "filesystem", .command = "npx",
              .args = {"-y", "@modelcontextprotocol/server-filesystem", QDir::currentPath()} },
        };
        return client->newSession(params);            // hands the MCP servers to the agent
    })
    .unwrap()
    .then(client, [client](const NewSessionResult &ns) {
        client->prompt(ns.sessionId, {ContentBlock::makeText("List the files in this repo.")});
    });

// Surface the agent's tool calls (filesystem/github/...) in the UI as they happen.
connect(client, &AcpClient::toolCallStarted,
    client, [](const QString &, const ToolCall &t) { /* t.title, t.kind, t.status */ });
```

> `McpServer` is stdio-only (`command`/`args`/`env`). To expose your **own** tools, run an
> LLMQore [`McpServer`](include/LLMQore/McpServer.hpp) over `McpStdioServerTransport` and point
> the agent at that binary in `mcpServers` — HTTP/SSE servers can't be passed this way.

- **Outgoing**: initialize, authenticate, session new/load/prompt/cancel/set_mode
- **Incoming**: streaming `session/update`, `session/request_permission`, `fs/*`, `terminal/*`
- **Providers**: `AcpPermissionProvider`, `AcpFileSystemProvider`, `AcpTerminalProvider`
  (with `DefaultFileSystemProvider`, `TerminalManager`, `CallbackPermissionProvider` defaults)
- **Agents**: `AcpAgentRegistry` loads named agents from external JSON ([`example/agents.json`](example/agents.json))
- **Auth**: agents authenticate themselves — no API key in the protocol. For Claude Code,
  set `CLAUDE_CODE_OAUTH_TOKEN` (from `claude setup-token`) in the agent's `env`; see
  [ACP authentication](docs/acp/authentication.md)

The GUI [`example-chat`](example/Main.qml) drives all of this — pick the **Claude Code (ACP)**
provider to launch and chat with a real agent. See the
[ACP implementation notes](docs/acp/implementation-notes.md) for the method-coverage matrix.

## Requirements

- C++20
- Qt 6.5+
- CMake 3.21+

## Documentation

- [Quick Start](docs/quick-start.md) — examples for LLM clients, tools, MCP server and client
- [Integration](docs/integration.md) — FetchContent and installed setup
- [MCP Bridge](docs/mcp-bridge.md) — aggregate stdio MCP servers behind one HTTP/SSE endpoint
- [MCP Protocol Coverage](docs/mcp/mcp_protocol_coverage.md) — spec-conformance matrix
- [ACP Host](docs/acp/architecture.md) — drive external ACP agents ([as-built notes](docs/acp/implementation-notes.md))
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
