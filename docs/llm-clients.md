# LLM clients

Reference for everything a client can do beyond [the Quick Start](quick-start.md).
Thread rules live in [threading.md](threading.md).

## Signals

| Signal | Fires |
|---|---|
| `chunkReceived` | each text delta as it streams |
| `accumulatedReceived` | the text so far, after each delta |
| `thinkingBlockReceived` | a reasoning block, once, with its continuation token |
| `toolStarted` / `toolResultReady` | around each tool execution |
| `requestFinalized` | before completion, with `CompletionInfo` |
| `requestCompleted` | the final text |
| `requestFailed` | an error; nothing else follows |

`requestFinalized` is emitted **before** `requestCompleted`, so a consumer resolving a
promise on finalization runs before any plain-text handler.

## CompletionInfo

```cpp
connect(client, &LLMQore::BaseClient::requestFinalized,
        this, [](const LLMQore::RequestID &, const LLMQore::CompletionInfo &info) {
    qDebug() << info.model << info.stopReason;
    if (info.usage)
        qDebug() << info.usage->promptTokens << info.usage->completionTokens
                 << info.usage->cachedPromptTokens << info.usage->reasoningTokens;
});
```

`info.conversation` is the history after every tool round — assign it back to your own
`Conversation` and the next turn carries everything. `info.requestPayload` is the JSON that
actually went out, for debugging.

`info.usage` is `std::optional`: absent means the provider sent no usage block, not zero.

## Streaming or not

```cpp
client->ask(conversation, {}, LLMQore::RequestMode::Buffered);
```

In `Buffered` mode the answer arrives in one piece. Claude, OpenAI and the Responses API
still emit a single `chunkReceived` carrying the whole text, so a handler written for
streaming keeps working; Google and Ollama emit only `accumulatedReceived`. Either way
`requestCompleted` carries the final text, and that is the signal to rely on when the mode
is not fixed.

Each client applies the mode the way its provider expects — a `stream` flag in the body for
Claude and OpenAI, a different endpoint (`:generateContent` against `:streamGenerateContent`)
for Google. Setting `stream` yourself has no effect; the mode wins.

## One-shot or streaming

`askOnce()` returns a future and skips the signals entirely:

```cpp
client->askOnce(conversation).then(this, [this](const LLMQore::CompletionInfo &result) {
    m_conversation = result.conversation;
    m_view->setPlainText(result.fullText);
}).onFailed(this, [this](const std::exception &e) {
    m_view->setPlainText(QString::fromUtf8(e.what()));
});
```

It resolves with the same `CompletionInfo` that `requestFinalized` carries, and rejects on
the same conditions that emit `requestFailed`. Tool rounds still run; the future settles
once, after the last one.

Use it for a single question with a single answer. Use `ask()` plus `accumulatedReceived`
or `chunkReceived` when the answer should appear as it is generated — the future cannot
deliver a partial result.

## Cancelling

```cpp
const LLMQore::RequestID id = client->ask(conversation);
client->cancelRequest(id);
```

## Models

```cpp
client->listModels().then([](const QList<LLMQore::ModelInfo> &models) {
    for (const auto &model : models)
        qDebug() << model.id << model.displayName;
});
```

`ModelInfo` carries limits and capabilities where the provider publishes them:

```cpp
struct ModelInfo {
    QString id;
    QString displayName;
    std::optional<int> maxOutputTokens;
    std::optional<int> maxInputTokens;
    std::optional<bool> supportsImageInput;
    std::optional<bool> supportsThinking;
    std::optional<bool> supportsToolCalls;
    std::optional<bool> supportsStructuredOutputs;
};
```

Every optional field is `nullopt` unless the provider's model endpoint reports it. Today
only Anthropic does; OpenAI, Google and Ollama return identifiers and little else. `nullopt`
means "not published", never "not supported".

A successful `listModels()` fills a cache on the client:

```cpp
client->listModels();
if (auto model = client->cachedModel("claude-opus-4-6"))
    qDebug() << model->maxOutputTokens.value_or(-1);
```

`ClaudeClient` uses that cache: once warm, a conversation request asks for the model's own
maximum instead of the built-in `kDefaultMaxTokens`. Nothing in the request path waits on
the network — an unwarmed cache just means the default.

## Multimodal turns

A turn is a list of content, not a string:

```cpp
QFile file("chart.png");
file.open(QIODevice::ReadOnly);

conversation.addUser({
    LLMQore::TextContent{"What does this chart show?"},
    LLMQore::ImageContent::fromBytes(file.readAll(), "image/png")});
```

Each client emits the shape its provider wants: `source.base64` for Claude, an `image_url`
content part for OpenAI, `input_image` for the Responses API, `inlineData` for Google, an
`images` array for Ollama. `ImageContent::fromUrl()` sends a link where the provider accepts
one and an inline copy where it does not.

## Thinking

```cpp
connect(client, &LLMQore::BaseClient::thinkingBlockReceived,
        this, [](const LLMQore::RequestID &, const QString &thinking, const QString &) {
    qDebug() << thinking.left(200);
});
```

Reasoning is also part of the conversation, and providers require their own opaque token
echoed back for it to stay valid — Claude's signature, Google's thought signature. Keeping
`info.conversation` keeps the token; the library never asks you to handle it.

The OpenAI Responses API is the exception, because replaying reasoning there also requires
`store: false`. It is opt-in:

```cpp
responses->setReasoningPersistence(
    LLMQore::OpenAIResponsesClient::ReasoningPersistence::Replay);
```

That single switch sets `store: false` (unless you set `store` yourself) and replays
reasoning items with their `encrypted_content`. Items that arrived without encrypted content
are skipped rather than sent in a form the API would reject.

## Gating tool execution

`ToolsManager` lives in `<LLMQore/Tools>`; including only `<LLMQore/Clients>` leaves it an
incomplete type.

Nothing runs a tool without the client's own loop deciding to — but you can put a gate in
front of it. A confirmation dialog, a policy check, a rate limit:

```cpp
client->tools()->setExecutionGate(
    [this](const QString &, const QString &, const QString &toolName, const QJsonObject &input)
        -> QFuture<bool> {
        if (toolName == "get_datetime")
            return LLMQore::readyFuture(true);
        return askTheUser(toolName, input);
    });
```

Returning a future that resolves to `false` denies the call; the model is told the tool
was refused and continues. The gate is consulted per call, not per round.

## Headers and authentication

Every client starts from its provider's profile: the headers the provider needs and an
`AuthScheme` saying where the key goes. All of it is replaceable, and `profile()` always
shows what the next request will carry.

`setHeader` changes one entry and leaves the rest alone:

```cpp
claude->setHeader("anthropic-beta", "extended-cache-ttl-2025-04-11");
```

`setHeaders` replaces the whole map, so list everything you still want:

```cpp
openRouter->setHeaders({
    {"Content-Type", "application/json"},
    {"HTTP-Referer", "https://myapp.example"},
    {"X-Title", "My App"},
});
```

`setAuthScheme` moves the credential:

```cpp
azure->setAuthScheme({.placement = LLMQore::AuthScheme::Placement::Header,
                      .name = "api-key"});
```

`Placement::QueryParam` puts it in the query string (Google's default), `Placement::None`
sends none. An empty `apiKey` sends nothing either way.

`setProfile` replaces the whole identity at once -- paths, headers, auth and logging
category -- so call it first and adjust afterwards. To change one field of a profile, start
from the current one: `auto p = client->profile(); p.chatPath = "..."; client->setProfile(p);`
keeps everything else, the key included.

## Non-default endpoints

Clients with more than one inference endpoint take a path as the second argument to
`sendMessage`. It is appended to the base URL, like the profile's default path, and an empty
string selects that default.

```cpp
auto *mistral = new LLMQore::OpenAIClient(
    "https://api.mistral.ai", "...", "codestral-latest", this);
mistral->setProfile(LLMQore::mistralProfile());

QJsonObject payload;
payload["model"] = "codestral-latest";
payload["prompt"] = "def fib(n):\n    ";
payload["suffix"] = "\n\nprint(fib(10))\n";

mistral->sendMessage(payload, "/v1/fim/completions");
```

Others that accept an override: `OllamaClient` (`/api/generate`, default `/api/chat`) and
`LlamaCppClient` (`/infill`, default `/v1/chat/completions`).

## The escape hatch

`ask(conversation, extra)` covers the common case. When you need to reshape the request
itself, build the payload, edit it, and send it:

```cpp
QJsonObject payload = client->buildConversationPayload(conversation);
payload["response_format"] = QJsonObject{{"type", "json_object"}};
client->sendMessage(payload);
```

`sendMessage(QJsonObject)` stays fully supported. Tool rounds, streaming and usage parsing
work the same way — only the conversation tracking in `CompletionInfo` is skipped, since the
library did not build the payload.

## Persisting a conversation

```cpp
const QJsonObject saved = conversation.toJson();
// ... later, possibly in another process, possibly against another provider
auto restored = LLMQore::Conversation::fromJson(saved);
```

The round trip preserves text, images, audio, tool calls, tool results and reasoning
tokens.
