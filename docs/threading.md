# Thread contract

Every LLMQore object — `BaseClient` and its subclasses, `ToolsManager`, `ToolRegistry`,
`ToolHandler`, `McpClient`, `McpServer`, `AcpClient` — lives on the thread of the `QObject`
parent passed to its constructor.

- **All public methods must be called from that thread.**
- **All signals are emitted on that thread.**

Debug builds enforce the first rule with `Q_ASSERT_X` on the public mutating and
raw-pointer-returning methods of those classes. A release build will not warn you; it will
race.

The one exception is `BaseClient::cancelRequest()`. It marshals itself onto the client's
thread, so it is safe to call from anywhere — cancelling from a UI thread while the client
runs on a worker is a supported pattern.

Providers follow the object they are handed to. The sampling client given to
`McpClient::setSamplingClient()`, the elicitation and roots providers given to `McpClient`,
and the resource and prompt providers given to `McpServer` must live on that client's or
server's thread. Their methods are called from that thread, and their signals —
`listChanged`, `resourceUpdated` — must be emitted there. Debug builds assert it when the
provider is installed.

The MCP HTTP transports follow the same rule together with the `HttpTransport` they send
through: the two must share a thread. Build them there, or move both with `moveToThread()`
before `start()`; moving them mid-session is not supported.

## Consuming from another thread

Connect with `Qt::AutoConnection` (the default). Qt queues the delivery and copies the
arguments, so a widget or model living on the GUI thread can observe a client running on a
worker thread without extra locking.

```cpp
connect(client, &LLMQore::BaseClient::chunkReceived,
        this, [this](const LLMQore::RequestID &, const QString &chunk) {
    m_view->append(chunk);
});
```

Everything crossing a signal is a value type — `Conversation`, `TurnContent`, `ToolResult`,
`CompletionInfo`, `ModelInfo`. Copying one is safe from any thread; none of them hand out a
pointer into library-owned storage.

## The one pointer rule

`ToolRegistry::registeredTools()` returns `QList<BaseTool *>` — raw pointers owned by the
registry. They are valid only until the next event-loop iteration, because removing a tool
deletes it.

For anything that outlives the immediate call — a UI list, a snapshot passed to another
thread — use `toolsSnapshot()`, which returns detached `ToolSnapshot` values:

```cpp
for (const auto &snapshot : client->tools()->toolsSnapshot())
    ui->addRow(snapshot.displayName, snapshot.description);
```

## Content blocks are values

Before 0.8.0, `BaseMessage` handed out `ContentBlock *` into a list that reallocated on
every append, so holding one across a parse step was a use-after-free waiting to happen.
Content is now a `std::variant` stored by value: `currentBlocks()` returns
`const QList<TurnContent> &`, and `currentToolUseContent()` /
`currentThinkingContent()` return copies. There is no pointer to outlive.

`currentBlocks()` still hands back a reference into the message's own storage, which the
next tool round clears and which dies with the message. Copy it before it crosses an
event-loop iteration or a thread boundary — the same rule `registeredTools()` follows above.
The two `current*Content()` accessors already return detached values, so they need no
such care.
