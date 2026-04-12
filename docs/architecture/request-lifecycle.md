# Request lifecycle

```mermaid
sequenceDiagram
    participant App as Host app
    participant CC as Provider<br/>(ClaudeClient, ...)
    participant BC as BaseClient
    participant HC as HttpClient
    participant HS as HttpStream
    participant SSE as SSEParser
    participant MSG as BaseMessage<br/>(e.g. ClaudeMessage)
    participant TM as ToolsManager
    participant BT as BaseTool<br/>(local or McpRemoteTool)

    App->>CC: sendMessage(payload, callbacks)
    CC->>BC: createRequest(callbacks)<br/>→ RequestID
    CC->>BC: sendRequest(id, url, payload, Streaming)
    BC->>BC: storeRequestContext(id, url, payload)
    BC->>HC: openStream(request, "POST", body)
    HC->>HS: HttpStream *
    Note right of HS: signals: headersReceived,<br/>chunkReceived, finished,<br/>errorOccurred

    HS-->>BC: headersReceived
    alt status in 2xx
        BC->>BC: errorMode = false
    else status 4xx/5xx
        BC->>BC: errorMode = true<br/>(capture body)
    end

    loop chunks arrive
        HS-->>BC: chunkReceived(bytes)
        alt errorMode
            BC->>BC: append to errorBody
        else normal
            BC->>CC: processData(id, bytes)
            CC->>SSE: append(bytes)
            SSE-->>CC: QList<SSEEvent>
            loop each event
                CC->>MSG: handleEvent (provider-specific)
                alt text_delta
                    CC->>BC: addChunk(id, text)
                    BC-->>App: onChunk(id, text)
                else tool_use delta
                    MSG->>MSG: accumulate ToolUseContent
                else thinking_delta
                    MSG->>MSG: accumulate ThinkingContent
                end
            end
        end
    end

    HS-->>BC: finished
    BC->>BC: onStreamFinished(id, error?)

    alt transport / HTTP error
        BC->>BC: parseHttpError(response)
        BC->>BC: cleanupFullRequest
        BC->>BC: failRequest(id, error)
        BC-->>App: onFailed(id, error)
    else message requires tool execution
        BC->>BC: executeToolsFromMessage(id)
        loop each ToolUseContent
            BC->>TM: executeToolCall(id, toolId, name, input)
            TM->>BT: executeAsync(input)
            BT-->>TM: QFuture<ToolResult>
        end
        TM-->>BC: toolExecutionComplete(id, QHash<String, ToolResult>)
        BC->>BC: handleToolContinuation(id, results)
        Note right of BC: Checks kMaxToolContinuations<br/>(= 10), increments counter
        BC->>CC: buildContinuationPayload(<br/>originalPayload, message, results)
        CC-->>BC: new payload
        BC->>BC: sendRequest(id, url, new payload, mode)
        Note over BC: Loops back to openStream above<br/>for the continuation turn
    else stream finished cleanly
        BC->>MSG: stopReason() → captured in ActiveRequest
        BC->>CC: cleanupDerivedData(id)
        BC->>BC: completeRequest(id)
        BC-->>App: onFinalized(id, CompletionInfo{fullText, model, stopReason})
        BC-->>App: onCompleted(id, fullText)
        BC-->>App: emit requestCompleted(id, fullText)
    end
```

---

## Phases

1. **Setup.** The provider subclass constructs its message object, registers it, and hands off to `BaseClient` which allocates a unique request ID, stores the caller's callbacks, and initiates the HTTP request.

2. **HTTP kickoff.** The base client chooses between a buffered one-shot request or a streaming connection depending on the requested mode. In streaming mode, the stream handle's signals are wired to internal handlers that look up the request context by ID.

3. **Header inspection.** A 2xx status means normal streaming proceeds. A 4xx or 5xx status switches the request into error mode, where incoming bytes are accumulated as an error body rather than being parsed as model output.

4. **Chunk loop.** In normal mode, each chunk of bytes is forwarded to the provider's stream parser, which pushes them through the appropriate framer (SSE or JSON-lines) to produce discrete events. Text deltas are forwarded to callbacks and signals immediately. Tool-use and thinking deltas accumulate silently inside the message object.

5. **Stream end.** Three outcomes are possible: an error (transport or HTTP) triggers failure notification and cleanup; pending tool-use blocks trigger the tool execution phase; otherwise the request completes normally with the stop reason captured from the message.

6. **Tool execution.** `ToolsManager` queues all pending tool calls, runs them asynchronously, and collects their results. Each tool produces a `ToolResult` (or an error result if it throws).

7. **Continuation.** After tool execution, the continuation counter is checked against the maximum (10). The provider builds a new payload incorporating the assistant's response and the tool results, and the request re-enters the HTTP phase under the same request ID.

8. **Final completion.** On clean completion, three notifications fire in order: a rich finalization callback (carrying full text, model name, and stop reason), a simple text completion callback, and the Qt completion signal.

9. **Cancellation.** Aborting a request tears down the stream, cleans up derived data, and delivers a failure notification. The destructor does the same for all pending requests.
