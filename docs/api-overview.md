# API Overview

LLMCore exposes two main abstractions to host applications: **`BaseClient`** for communicating with LLM providers, and **`BaseTool`** for extending model capabilities with custom tools.

## BaseClient

`BaseClient` is the abstract base class for every LLM provider. It handles sending messages to a provider, receiving streamed responses, and automatically executing tool calls when the model requests them. Host code interacts with a provider client (such as `ClaudeClient` or `OpenAIClient`) through two complementary notification mechanisms:

- **Qt signals** -- broadcast to any number of listeners. Cover streamed text chunks, accumulated text, request completion/failure, thinking blocks, and tool execution events.
- **RequestCallbacks** -- per-request callback struct passed to the send call. Delivers the same events but scoped to a single request, which is convenient when multiplexing several requests on one client.

See `include/LLMCore/BaseClient.hpp` for the full API surface.

## BaseTool

`BaseTool` is the interface for custom tools that models can invoke. Each tool declares an identifier, a human-readable name, a description (sent to the model), and a JSON Schema for its parameters. The single async entry point returns a `QFuture<ToolResult>` carrying rich content (text, images, audio, resources, or resource links). Tools can be enabled or disabled at runtime without removing them from the registry.

See `include/LLMCore/BaseTool.hpp` for the full interface.
