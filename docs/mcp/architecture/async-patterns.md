# Async chain model

No `QFutureWatcher + QPromise + deleteLater` by hand in the MCP layer. Only these patterns:

```mermaid
flowchart LR
    subgraph Pattern1["Simple transform"]
        direction TB
        A1["send request"]
        A2["transform result"]
        A3["QFuture~T~"]
        A1 --> A2 --> A3
    end

    subgraph Pattern2["Guarded method"]
        direction TB
        B1["guarded request"]
        B2{{"initialized?"}}
        B3["send via session"]
        B4["reject with ProtocolError"]
        B5["transform result"]
        B6["QFuture~T~"]
        B1 --> B2
        B2 -->|yes| B3 --> B5
        B2 -->|no| B4 --> B5
        B5 --> B6
    end

    subgraph Pattern3["Typed failure recovery"]
        direction TB
        C1["source future"]
        C2["handle success"]
        C3["catch McpException"]
        C4["catch std::exception"]
        C5["QFuture~T~"]
        C1 --> C2 --> C3 --> C4 --> C5
    end

    subgraph Pattern4["Parallel collect"]
        direction TB
        D1["invoke all providers"]
        D2["wait for all futures"]
        D3["merge results"]
        D4["QFuture~QJsonValue~"]
        D1 --> D2 --> D3 --> D4
    end

    subgraph Pattern5["Sequential try-each"]
        direction TB
        E1["shared state<br/>(heap-allocated)"]
        E2["attempt next provider"]
        E3["run provider"]
        E4["extract result"]
        E5{{"has result?"}}
        E6["resolve promise"]
        E7["advance to next"]
        E8["on failure"]
        E1 --> E2 --> E3 --> E4 --> E5
        E5 -->|yes| E6
        E5 -->|no| E7
        E3 --> E8 --> E7
    end
```

## Patterns

1. **Simple transform** — used by tool listing, prompt retrieval, etc. Parse JSON response into typed result.

2. **Guarded method** — all initialized-only operations go through an initialization check first.

3. **Typed failure recovery** — used by handshake and remote tool execution. First handler catches MCP-specific exceptions (preserves concrete type); second catches generic exceptions and normalises to McpException.

4. **Parallel collect** — McpServer collects from all providers in parallel, merges into a single JSON array. Used for `resources/list`, `resources/templates/list`, `prompts/list`, `completion/complete`.

5. **Sequential try-each** — McpServer tries providers one at a time until one returns a result. Heap-allocated shared state with recursive chaining. Used for `resources/read`, `prompts/get`.

## Rules for new code

- **Always pass `this` as context** to `.then(ctx, lambda)` and `.onFailed(ctx, lambda)` — Qt context-aware disconnection.
- **Never manually `new QFutureWatcher`** — use `.then()` chain instead.
- **`e.raise()` to re-propagate** — `std::make_exception_ptr(e)` silently slices.
- **Long-running state across suspensions** — `std::shared_ptr` on heap, captured by value. Only justified for complex flows like `TryEachState`.
