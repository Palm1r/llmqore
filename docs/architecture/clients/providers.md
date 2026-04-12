# Provider cheatsheet

## Summary table

| Provider | Client class | Message class | Stream framing | Auth style |
|---|---|---|---|---|
| Anthropic Claude | `ClaudeClient` | `ClaudeMessage` | SSE | API key header + version header |
| OpenAI Chat Completions | `OpenAIClient` | `OpenAIMessage` | SSE | Bearer token |
| OpenAI Responses | `OpenAIResponsesClient` | `OpenAIResponsesMessage` | SSE | Bearer token |
| Google AI (Gemini) | `GoogleAIClient` | `GoogleMessage` | SSE (query param) | API key in query string |
| Ollama | `OllamaClient` | `OllamaMessage` | JSON-lines (`LineBuffer`) | Bearer token (optional) |
| llama.cpp | `LlamaCppClient` | reuses `OpenAIMessage` | SSE | Bearer token (optional) |

---

## Claude (`source/clients/claude/`)

Claude uses SSE streaming with a proprietary event structure. The message parser accumulates content blocks from delta events covering text, tool input JSON, and thinking content (including signatures). Tool results support rich content -- images survive across continuations. The error parser extracts messages from Anthropic's error envelope format. A default max-tokens value is injected by the convenience send method because Anthropic requires it.

## OpenAI Chat Completions (`source/clients/openai/OpenAI{Client,Message}.cpp`)

The standard OpenAI chat completions endpoint. The message parser reads from the delta structure within the choices array. Tool results are text-only -- rich content (images, audio) is flattened to a textual description before being sent back to the model. The error parser handles OpenAI's error object format.

## OpenAI Responses (`source/clients/openai/OpenAIResponses{Client,Message}.cpp`)

The newer OpenAI Responses API with a different payload shape and its own event namespace. Supports rich tool results including images and structured outputs. Uses a separate tool schema format from Chat Completions. Preferred for newer model families and multi-modal use cases; default to `OpenAIClient` for third-party API clones.

## Google AI / Gemini (`source/clients/google/`)

Streams via SSE with the API key passed as a query parameter. The message parser reads from the candidates/parts structure, handling text, function calls, and safety blocks. Tool results are rich -- function responses carry structured data and images are sent as inline data. The error parser handles Google's error object format with code, message, and status fields. Google has the most varied set of finish reasons, including safety and content-policy categories.

## Ollama (`source/clients/ollama/`)

Uses JSON-lines framing instead of SSE, processed through `LineBuffer`. Each line is a complete JSON object with a done flag. Supports both chat and generate endpoints depending on payload shape. Tool results are text-only. Auth is optional. Streaming is enabled by default; buffered mode disables it in the payload.

## llama.cpp (`source/clients/llamacpp/`)

Talks to a local `llama-server` instance. OpenAI-compatible -- reuses `OpenAIMessage` and the OpenAI tool schema format. Adds support for a fill-in-the-middle endpoint alongside the standard chat completions endpoint, switching between them based on payload inspection. Also exposes diagnostic endpoints for health and server properties.
