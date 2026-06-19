# Prompt turn: streaming + callbacks

The heart of the ACP host. One `session/prompt` request stays outstanding for the
whole turn. While it is pending, the agent (a) streams its thinking and answer back as
`session/update` **notifications**, and (b) makes **requests** back into the host
(permission, file system, terminal). The turn ends when the original `session/prompt`
request resolves with a `stopReason`.

```mermaid
sequenceDiagram
    participant App as Host app / chat UI
    participant AC as AcpClient
    participant JS as JsonRpcSession
    participant EA as Agent process
    participant PP as PermissionProvider
    participant FSP as FileSystemProvider

    App->>AC: prompt(sessionId, [text block, …])
    AC->>JS: request "session/prompt"<br/>(sessionId, prompt: ContentBlock[])
    JS->>EA: send (one outstanding request)

    loop Agent works (notifications, no reply)
        EA-->>JS: notify "session/update"
        JS->>AC: route by sessionId + sessionUpdate tag
        alt agent_message_chunk
            AC-->>App: agentMessageChunk(text)
        else agent_thought_chunk
            AC-->>App: agentThoughtChunk(text)
        else tool_call
            AC->>AC: SessionState.tools[toolCallId] = ToolCall
            AC-->>App: toolCallStarted(ToolCall)
        else tool_call_update
            AC->>AC: merge into existing ToolCall
            AC-->>App: toolCallUpdated(ToolCall)
        else plan
            AC-->>App: planUpdated(entries)
        else available_commands_update / current_mode_update
            AC-->>App: commandsUpdated / modeChanged
        end
    end

    Note over EA,FSP: Mid-turn, the agent calls back into the host (requests, need replies)
    EA->>JS: request "fs/read_text_file"(path, line?, limit?)
    JS->>AC: incoming request
    AC->>FSP: readTextFile(sessionId, path, line, limit)
    FSP-->>AC: content (QFuture resolves)
    AC->>JS: respond { content }
    JS->>EA: result

    EA->>JS: request "session/request_permission"<br/>(toolCall, options[])
    JS->>AC: incoming request
    AC->>PP: requestPermission(sessionId, toolCall, options)
    PP-->>App: show prompt, await user
    App-->>PP: user picks optionId
    PP-->>AC: outcome { selected, optionId }
    AC->>JS: respond { outcome }
    JS->>EA: result

    EA->>JS: request "fs/write_text_file"(path, content)
    JS->>AC: incoming request
    AC->>FSP: writeTextFile(...)
    FSP-->>AC: ok
    AC->>JS: respond {}

    Note over EA,App: Turn ends
    EA-->>JS: respond to "session/prompt"<br/>{ stopReason }
    JS-->>AC: resolve prompt promise
    AC-->>App: promptFinished(sessionId, stopReason)
```

## Cancellation

```mermaid
sequenceDiagram
    participant App as Host app
    participant AC as AcpClient
    participant JS as JsonRpcSession
    participant EA as Agent process

    App->>AC: cancel(sessionId)
    AC->>JS: notify "session/cancel"(sessionId)
    JS->>EA: send (no response expected)
    Note over EA: agent aborts work,<br/>may still flush a few updates
    EA-->>JS: respond to in-flight "session/prompt"<br/>{ stopReason: "Cancelled" }
    JS-->>AC: resolve prompt promise
    AC-->>App: promptFinished(sessionId, "Cancelled")
```

`cancel()` does **not** resolve the turn itself. It signals intent; the turn still
ends through the normal `session/prompt` resolution, now carrying
`stopReason: "Cancelled"`. The host must keep servicing updates/requests until then.

## `stopReason` values

| Value | Meaning |
|---|---|
| `end_turn` | Agent finished the turn normally. |
| `cancelled` | Turn ended because of a prior `session/cancel`. |
| `max_tokens` | Token budget for the turn was exhausted. |
| `max_turn_requests` | Per-turn model-request cap was hit. |
| `refusal` | Agent refused to continue. |

(Verified live: Claude Code returns `end_turn`. An earlier draft listed
`Completed`/`Error`, which the spec does not use.)

## ToolCall lifecycle on the host side

A `tool_call` update creates a `ToolCall` entry; subsequent `tool_call_update`s are
**merged** into it by `toolCallId` (status `Pending → Running → Completed | Failed`,
plus growing `content` and `locations`). The host UI renders one evolving card per
`toolCallId`, not one card per notification. `SessionState.tools` holds the
authoritative merged state, cleared when the turn ends.

## Invariants

- **Exactly one `session/prompt` is outstanding per session at a time.** A second
  `prompt()` on a busy session is rejected by `AcpClient` before hitting the wire.
- **`session/update` carries no reply.** Dropping or mis-routing one cannot deadlock
  the turn; only the `session/prompt` response can end it.
- **Agent→host requests block the agent until answered.** Provider futures must
  always resolve (success or error) — a hung provider hangs the agent's turn. Provide
  timeouts in long-running providers (notably terminal waits).
- **Permission outcome is mandatory.** If the user closes the prompt without choosing,
  the provider resolves with `outcome: { type: "cancelled" }`, never leaves it pending.
