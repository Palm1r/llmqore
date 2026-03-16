# LLMCore

[![Build and Test](https://github.com/Palm1r/llmcore/actions/workflows/build_and_test.yml/badge.svg?branch=main)](https://github.com/Palm1r/llmcore/actions/workflows/build_and_test.yml)
![GitHub Tag](https://img.shields.io/github/v/tag/Palm1r/llmcore)

Qt/C++ library for integrating LLM providers into Qt applications. Provides a unified streaming API across multiple backends with built-in tool calling support.

> **Note:** The library does not cover the full API surface of each provider. Missing features will be added over time.

## Supported Providers

| Provider | Client class | Streaming | Tools | Thinking |
|---|---|---|---|---|
| Anthropic Claude | `ClaudeClient` | ✓ | ✓ | ✓ |
| OpenAI (Chat Completions) | `OpenAIClient` | ✓ | ✓ | — |
| OpenAI (Responses API) | `OpenAIResponsesClient` | ✓ | ✓ | ✓ |
| Ollama | `OllamaClient` | ✓ | ✓ | ✓ |
| Google AI (Gemini) | `GoogleAIClient` | ✓ | ✓ | ✓ |
| llama.cpp | `LlamaCppClient` | ✓ | ✓ | ✓ |

## Requirements

- C++20
- Qt 6.5+
- CMake 3.21+

## Documentation

- [Integration](docs/integration.md) — FetchContent and installed setup
- [Quick Start](docs/quick-start.md) — send your first request
- [Tool Calling](docs/tool-calling.md) — register and use tools
- [API Overview](docs/api-overview.md) — public classes, signals, callbacks

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