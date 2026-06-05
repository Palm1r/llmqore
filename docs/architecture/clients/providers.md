# Provider cheatsheet

## Summary table

| Provider | Client class | Message class | Stream framing | Auth style |
|---|---|---|---|---|
| Anthropic Claude | `ClaudeClient` | `ClaudeMessage` | SSE | API key header + version header |
| OpenAI Chat Completions | `OpenAIClient` | `OpenAIMessage` | SSE | Bearer token |
| OpenAI Responses | `OpenAIResponsesClient` | `OpenAIResponsesMessage` | SSE | Bearer token |
| Google AI (Gemini) | `GoogleAIClient` | `GoogleMessage` | SSE (query param) | API key in query string |
| Mistral | `MistralClient` | reuses `OpenAIMessage` | SSE | Bearer token |
| DeepSeek | `OpenAIClient` (direct) | reuses `OpenAIMessage` | SSE | Bearer token |
| Ollama | `OllamaClient` | `OllamaMessage` | JSON-lines (`LineBuffer`) | Bearer token (optional) |
| llama.cpp | `LlamaCppClient` | reuses `OpenAIMessage` | SSE | Bearer token (optional) |

---

## Claude (`source/clients/claude/`)

Claude uses SSE streaming with a proprietary event structure. The message parser accumulates content blocks from delta events covering text, tool input JSON, and thinking content (including signatures). Tool results support rich content -- images survive across continuations. The error parser extracts messages from Anthropic's error envelope format. A default max-tokens value is injected by the convenience send method because Anthropic requires it.

## OpenAI Chat Completions (`source/clients/openai/OpenAI{Client,Message}.cpp`)

The standard OpenAI chat completions endpoint. The message parser reads from the delta structure within the choices array. Tool results are text-only -- rich content (images, audio) is flattened to a textual description before being sent back to the model. The error parser handles OpenAI's error object format.

Two reasoning shapes are surfaced as `ThinkingContent` (emitted via `thinkingBlockReceived`): a top-level `reasoning_content` string (DeepSeek-style) and an array-form `content` whose entries carry `thinking` and `text` chunks (Mistral Magistral). A plain string `content` is treated as answer text as usual. Both paths apply in streaming and buffered mode, so any OpenAI-compatible provider using either convention works without a dedicated client.

## OpenAI Responses (`source/clients/openai/OpenAIResponses{Client,Message}.cpp`)

The newer OpenAI Responses API with a different payload shape and its own event namespace. Supports rich tool results including images and structured outputs. Uses a separate tool schema format from Chat Completions. Preferred for newer model families and multi-modal use cases; default to `OpenAIClient` for third-party API clones.

## Google AI / Gemini (`source/clients/google/`)

Streams via SSE with the API key passed as a query parameter. The message parser reads from the candidates/parts structure, handling text, function calls, and safety blocks. Tool results are rich -- function responses carry structured data and images are sent as inline data. The error parser handles Google's error object format with code, message, and status fields. Google has the most varied set of finish reasons, including safety and content-policy categories.

## Mistral (`source/clients/mistral/`)

OpenAI-compatible -- subclasses `OpenAIClient`, reuses `OpenAIMessage` and the OpenAI tool schema format. Defaults to `/chat/completions`; pass `/fim/completions` as the `endpoint` argument to `sendMessage` to target the Codestral fill-in-the-middle endpoint. Bearer-token auth. Works with the standard OpenAI error envelope handling from the base class.

Reasoning models (Magistral) reason natively -- no `reasoning_effort` parameter is needed. Their `content` arrives as an array of chunks: `thinking` entries (whose `thinking` field is a string or a nested array of `{type:"text", text}` parts) and `text` entries for the final answer. `OpenAIClient` parses these inherited paths, routing the reasoning trace to `ThinkingContent` / `thinkingBlockReceived` and the answer to normal text deltas.

## DeepSeek (via `OpenAIClient`)

OpenAI-compatible with no dedicated client class -- instantiate `OpenAIClient` directly with DeepSeek's base URL and API key. Reasoning models (`deepseek-reasoner`) stream the chain-of-thought in a separate top-level `reasoning_content` field alongside the regular `content`; `OpenAIClient` surfaces it as `ThinkingContent` via `thinkingBlockReceived`, while `content` flows through as the answer. Bearer-token auth and the standard OpenAI error envelope apply.

## Ollama (`source/clients/ollama/`)

Uses JSON-lines framing instead of SSE, processed through `LineBuffer`. Each line is a complete JSON object with a done flag. Defaults to `/api/chat`; pass `/api/generate` as the `endpoint` argument to `sendMessage` when building a prompt-based generation payload. Tool results are text-only. Auth is optional. Streaming is enabled by default; buffered mode disables it in the payload.

## llama.cpp (`source/clients/llamacpp/`)

Talks to a local `llama-server` instance. OpenAI-compatible -- reuses `OpenAIMessage` and the OpenAI tool schema format. Defaults to `/v1/chat/completions`; pass `/infill` as the `endpoint` argument to `sendMessage` to target the fill-in-the-middle endpoint. Also exposes diagnostic endpoints for health and server properties.
