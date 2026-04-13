# LLMCore

[![Build and Test](https://github.com/Palm1r/llmcore/actions/workflows/build_and_test.yml/badge.svg?branch=main)](https://github.com/Palm1r/llmcore/actions/workflows/build_and_test.yml)
![GitHub Tag](https://img.shields.io/github/v/tag/Palm1r/llmcore)

Qt/C++ library for working with LLM providers and MCP servers. Streaming chat, tool calling, and a full MCP 2025-11-25 client/server — all in one library.

**LLM clients** — unified streaming API across six providers:

```cpp
auto *client = new LLMCore::ClaudeClient(url, apiKey, model, this);
client->ask("What is Qt?", cb);
```

**MCP server** — expose tools, resources and prompts over stdio or HTTP:

```cpp
// stdio (stdin/stdout, e.g. for Claude Desktop)
auto *transport = new LLMCore::McpStdioServerTransport(&app);

// or Streamable HTTP
auto *transport = new LLMCore::McpHttpServerTransport({.port = 8080, .path = "/mcp"}, &app);

auto *server = new LLMCore::McpServer(transport, cfg, &app);
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

## Supported Providers

| Provider | Client class | Streaming | Tools | Thinking |
|---|---|---|---|---|
| Anthropic Claude | `ClaudeClient` | ✓ | ✓ | ✓ |
| OpenAI (Chat Completions) | `OpenAIClient` | ✓ | ✓ | — |
| OpenAI (Responses API) | `OpenAIResponsesClient` | ✓ | ✓ | ✓ |
| Ollama | `OllamaClient` | ✓ | ✓ | ✓ |
| Google AI | `GoogleAIClient` | ✓ | ✓ | ✓ |
| llama.cpp | `LlamaCppClient` | ✓ | ✓ | ✓ |

## MCP (Model Context Protocol)

Client and server implementation of the MCP 2025-11-25 spec:

- **Transports**: stdio, Streamable HTTP
- **Server**: tools, resources, resource templates, prompts, completions, sampling, elicitation
- **Client**: tools, resources, prompts, completions, sampling, elicitation, roots

See [MCP Protocol Coverage](docs/mcp/mcp_protocol_coverage.md) for the full spec-conformance matrix.

## Requirements

- C++20
- Qt 6.5+
- CMake 3.21+

## Documentation

- [Quick Start](docs/quick-start.md) — examples for LLM clients, tools, MCP server and client
- [Integration](docs/integration.md) — FetchContent and installed setup
- [MCP Protocol Coverage](docs/mcp/mcp_protocol_coverage.md) — spec-conformance matrix
- [Architecture](docs/architecture.md) — internals, for contributors

## Support

- **Report Issues**: [open an issue](https://github.com/Palm1r/llmcore/issues) on GitHub
- **Contribute**: pull requests with bug fixes or new features are welcome
- **Spread the Word**: star the repository and share with fellow developers
- **Financial Support**:
   - Bitcoin (BTC): `bc1qndq7f0mpnlya48vk7kugvyqj5w89xrg4wzg68t`
   - Ethereum (ETH): `0xA5e8c37c94b24e25F9f1f292a01AF55F03099D8D`
   - Litecoin (LTC): `ltc1qlrxnk30s2pcjchzx4qrxvdjt5gzuervy5mv0vy`
   - USDT (TRC20): `THdZrE7d6epW6ry98GA3MLXRjha1DjKtUx`

## License

MIT — see [LICENSE](LICENSE).
