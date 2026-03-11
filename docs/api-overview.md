# API Overview

## BaseClient

| Method | Description |
|---|---|
| `sendMessage(payload, callbacks)` | Send a JSON payload, returns `RequestID` |
| `ask(prompt, callbacks)` | Convenience: send a text prompt |
| `cancelRequest(id)` | Cancel an in-flight request |
| `tools()` | Access the `ToolsManager` for registering tools |
| `hasTools()` | Check if any tools are registered |

## Signals

| Signal | When |
|---|---|
| `chunkReceived(id, chunk)` | Each streamed text chunk |
| `accumulatedReceived(id, text)` | Full text accumulated so far |
| `requestCompleted(id, fullText)` | Stream finished successfully |
| `requestFailed(id, error)` | Request failed or was cancelled |
| `thinkingBlockReceived(id, thinking, signature)` | Thinking block completed |
| `toolStarted(id, toolId, toolName)` | Tool execution began |
| `toolResultReady(id, toolId, toolName, result)` | Tool execution finished |

## RequestCallbacks

Same events as signals but scoped to a single request: `onChunk`, `onAccumulated`, `onCompleted`, `onFailed`, `onThinkingBlock`, `onToolStarted`, `onToolResult`.

## BaseTool

| Method | Description |
|---|---|
| `id()` | Unique tool identifier |
| `displayName()` | Human-readable name |
| `description()` | Description sent to the model |
| `parametersSchema()` | JSON Schema for tool parameters |
| `executeAsync(input)` | Async execution, returns `QFuture<QString>` |
| `isEnabled()` / `setEnabled(bool)` | Enable or disable the tool |