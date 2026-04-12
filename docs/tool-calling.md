# Tool Calling

## Creating a tool

Subclass `BaseTool` and implement the virtual methods:

```cpp
#include <LLMCore/Tools>

class WeatherTool : public LLMCore::BaseTool
{
    Q_OBJECT
public:
    explicit WeatherTool(QObject *parent = nullptr)
        : BaseTool(parent) {}

    QString id() const override { return "get_weather"; }
    QString displayName() const override { return "Weather"; }
    QString description() const override {
        return "Get current weather for a city.";
    }

    QJsonObject parametersSchema() const override {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"city", QJsonObject{{"type", "string"},
                                     {"description", "City name"}}}
            }},
            {"required", QJsonArray{"city"}}
        };
    }

    QFuture<LLMCore::ToolResult> executeAsync(const QJsonObject &input) override {
        return QtConcurrent::run([input]() -> LLMCore::ToolResult {
            QString city = input["city"].toString();
            // ... fetch weather data ...
            return LLMCore::ToolResult::text(
                QString("{\"temp\": 22, \"city\": \"%1\"}").arg(city));
        });
    }
};
```

`executeAsync()` returns a `LLMCore::ToolResult`, a rich envelope that matches
the MCP tools/call result shape. Most tools just wrap a single string via
`ToolResult::text("...")` or signal a failure via `ToolResult::error("...")`.
If you need to return images, audio, or embedded/linked resources, build the
content list directly:

```cpp
LLMCore::ToolResult r;
r.content.append(LLMCore::ToolContent::makeText("Here is the chart:"));
r.content.append(LLMCore::ToolContent::makeImage(pngBytes, "image/png"));
return r;
```

Rich content is preserved end-to-end when exposing the tool via `McpServer`.
The non-MCP LLM continuation path (Claude/OpenAI/…) currently flattens
non-text blocks into placeholder strings via `ToolResult::asText()`.

## Registering and using tools

```cpp
client->tools()->addTool(new WeatherTool(client));

QJsonArray toolsDefs = client->tools()->getToolsDefinitions();
QJsonObject payload;
payload["tools"] = toolsDefs;
payload["messages"] = QJsonArray{
    QJsonObject{{"role", "user"},
                {"content", "What's the weather in Belgrade?"}}
};

client->sendMessage(payload, cb);
// When the model calls a tool, LLMCore executes it and
// sends results back automatically (tool continuation loop).
```

## Tool lifecycle signals

```cpp
connect(client, &LLMCore::BaseClient::toolStarted,
        this, [](const auto &, const auto &, const QString &name) {
    qDebug() << "Tool started:" << name;
});
connect(client, &LLMCore::BaseClient::toolResultReady,
        this, [](const auto &, const auto &, const auto &name, const auto &result) {
    qDebug() << "Tool result:" << name << result;
});
```