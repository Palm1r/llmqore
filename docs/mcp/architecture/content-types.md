# Content type hierarchy

Two separate hierarchies:

- **`ContentBlock` family** (`ContentBlocks.hpp`) — model **output** segments in `BaseMessage`. Polymorphic, heap-allocated, `QList<ContentBlock *>`.
- **`ToolContent` + `ToolResult`** (`ToolResult.hpp`) — tool **results**. Value-typed, JSON-serialised, matches MCP `tools/call` wire format.

```mermaid
classDiagram
    class ContentBlock {
        <<abstract>>
    }

    class TextContent
    class ImageContent
    class ToolUseContent
    class ToolResultContent
    class ThinkingContent
    class RedactedThinkingContent

    ContentBlock <|-- TextContent
    ContentBlock <|-- ImageContent
    ContentBlock <|-- ToolUseContent
    ContentBlock <|-- ToolResultContent
    ContentBlock <|-- ThinkingContent
    ContentBlock <|-- RedactedThinkingContent

    class BaseMessage {
        <<abstract>>
        // state tracking + block accumulation
    }

    BaseMessage --> ContentBlock : owns many

    class ToolContent {
        <<struct>>
        // typed payload (text, data, mimeType, uri, ...)
        // JSON round-trip
    }

    class ToolResult {
        <<struct>>
        // list of ToolContent + error flag
        // factory methods: text, error
        // JSON serialization
    }

    ToolResult --> ToolContent : has many
```

## How the two hierarchies meet

1. `BaseMessage` accumulates `ContentBlock` instances during SSE stream.
2. Stream ends with `RequiresToolExecution` → `BaseClient` walks `ToolUseContent` → `ToolsManager`.
3. Tools run → `toolExecutionComplete(requestId, QHash<QString, ToolResult>)`.
4. `buildContinuationPayload` uses **both**: assistant turn from `BaseMessage` blocks + user turn from `ToolResult` envelopes. Rich providers (Claude, OpenAI Responses, Google) preserve images; text-only (Ollama, OpenAI Chat) flatten via `asText()`.

## Extending

- **New model output shape** (e.g. thinking variant) → add `ContentBlock` subclass, teach provider's `BaseMessage` parser.
- **New tool result shape** (e.g. video) → add `ToolContent::Type` value, teach each provider's `createToolResult*` methods. MCP wire layer handles it automatically via `ToolResult::toJson()`.
