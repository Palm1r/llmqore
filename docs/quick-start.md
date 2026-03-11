# Quick Start

## Minimal example

```cpp
#include <LLMCore/Clients>

auto *client = new LLMCore::ClaudeClient(
    "https://api.anthropic.com", "sk-...", "claude-sonnet-4-20250514", this);

LLMCore::RequestCallbacks cb;
cb.onChunk = [](const LLMCore::RequestID &, const QString &chunk) {
    qDebug() << chunk;
};
cb.onCompleted = [](const LLMCore::RequestID &, const QString &full) {
    qDebug() << "Done:" << full;
};
cb.onFailed = [](const LLMCore::RequestID &, const QString &err) {
    qWarning() << "Error:" << err;
};

client->ask("What is Qt?", cb);
```

## Full payload control

```cpp
QJsonObject payload;
payload["model"] = "claude-sonnet-4-20250514";
payload["max_tokens"] = 4096;
payload["stream"] = true;
payload["messages"] = QJsonArray{
    QJsonObject{{"role", "user"}, {"content", "Explain RAII in C++"}}
};

client->sendMessage(payload, cb);
```

## Using signals instead of callbacks

```cpp
auto *client = new LLMCore::OpenAIClient(url, apiKey, model, this);

connect(client, &LLMCore::BaseClient::chunkReceived,
        this, [](const LLMCore::RequestID &, const QString &chunk) {
    qDebug() << chunk;
});

connect(client, &LLMCore::BaseClient::requestCompleted,
        this, [](const LLMCore::RequestID &, const QString &full) {
    qDebug() << "Done:" << full;
});

client->ask("Hello!");
```

## Thinking / reasoning blocks

```cpp
LLMCore::RequestCallbacks cb;
cb.onThinkingBlock = [](const LLMCore::RequestID &,
                         const QString &thinking,
                         const QString &signature) {
    qDebug() << "Thinking:" << thinking.left(200) << "...";
};
```

## Cancel a request

```cpp
LLMCore::RequestID id = client->ask("Write a long essay...", cb);
// ...later:
client->cancelRequest(id);
```