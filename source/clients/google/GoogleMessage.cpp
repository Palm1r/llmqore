// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include "GoogleMessage.hpp"

#include <QJsonDocument>
#include <QStringList>
#include <QUuid>

#include <LLMQore/Log.hpp>

#include "core/ErrorEnvelope.hpp"

namespace LLMQore {

namespace {

QJsonValue sanitizeSchemaValueForGoogle(const QJsonValue &value);

QJsonObject sanitizeSchemaForGoogle(const QJsonObject &schema)
{
    static const QSet<QString> kUnsupported{
        QStringLiteral("$schema"),
        QStringLiteral("$id"),
        QStringLiteral("$ref"),
        QStringLiteral("$defs"),
        QStringLiteral("definitions"),
        QStringLiteral("additionalProperties"),
        QStringLiteral("patternProperties"),
        QStringLiteral("unevaluatedProperties"),
        QStringLiteral("dependencies"),
        QStringLiteral("dependentSchemas"),
        QStringLiteral("dependentRequired"),
        QStringLiteral("allOf"),
        QStringLiteral("oneOf"),
        QStringLiteral("not"),
        QStringLiteral("const"),
    };

    QJsonObject result;
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        if (kUnsupported.contains(it.key()))
            continue;
        result.insert(it.key(), sanitizeSchemaValueForGoogle(it.value()));
    }
    return result;
}

QJsonValue sanitizeSchemaValueForGoogle(const QJsonValue &value)
{
    if (value.isObject())
        return sanitizeSchemaForGoogle(value.toObject());
    if (value.isArray()) {
        QJsonArray out;
        const QJsonArray arr = value.toArray();
        for (const auto &item : arr)
            out.append(sanitizeSchemaValueForGoogle(item));
        return out;
    }
    return value;
}

class GoogleToolDialect : public ToolDialect
{
public:
    QJsonObject wrapDefinition(const BaseTool &tool) const override
    {
        return QJsonObject{
            {"name", tool.id()},
            {"description", tool.description()},
            {"parameters", sanitizeSchemaForGoogle(tool.parametersSchema())}};
    }

    QJsonArray finalizeDefinitions(QJsonArray definitions) const override
    {
        if (definitions.isEmpty())
            return definitions;
        return QJsonArray{QJsonObject{{"function_declarations", definitions}}};
    }
};

} // namespace

const ToolDialect &GoogleMessage::toolDialect()
{
    static const GoogleToolDialect dialect;
    return dialect;
}


GoogleMessage::GoogleMessage(QObject *parent)
    : BaseMessage(parent)
{}

MessageEffects GoogleMessage::applyEvent(const QJsonObject &chunk)
{
    MessageEffects effects;

    if (const std::optional<QJsonObject> error = errorIn(chunk)) {
        effects.error = *error;
        return effects;
    }

    if (chunk.value("usageMetadata").isObject())
        effects.usage = chunk;

    const QJsonArray candidates = chunk.value("candidates").toArray();
    for (const QJsonValue &candidate : candidates) {
        const QJsonObject candidateObj = candidate.toObject();

        const QJsonArray parts
            = candidateObj.value("content").toObject().value("parts").toArray();
        for (const QJsonValue &part : parts) {
            const QJsonObject partObj = part.toObject();

            if (partObj.contains("text")) {
                const QString text = partObj.value("text").toString();
                if (partObj.value("thought").toBool(false)) {
                    handleThoughtDelta(text);
                    if (partObj.contains("signature"))
                        handleThoughtSignature(partObj.value("signature").toString());
                } else {
                    effects.thinkingCompleted = true;
                    handleContentDelta(text);
                    effects.chunk += text;
                }
            }

            if (partObj.contains("thoughtSignature"))
                handleThoughtSignature(partObj.value("thoughtSignature").toString());

            if (partObj.contains("functionCall")) {
                effects.thinkingCompleted = true;

                const QJsonObject functionCall = partObj.value("functionCall").toObject();
                handleToolCallStart(functionCall.value("name").toString());
                handleToolCallDelta(QString::fromUtf8(
                    QJsonDocument(functionCall.value("args").toObject())
                        .toJson(QJsonDocument::Compact)));
                handleToolCallComplete();
            }
        }

        if (candidateObj.contains("finishReason")) {
            handleStopReason(candidateObj.value("finishReason").toString());
            if (isErrorFinishReason()) {
                qCDebug(llmGoogleLog).noquote()
                    << QString("Google AI error: %1").arg(errorFinishMessage());
                effects.error = QJsonObject{{QStringLiteral("message"), errorFinishMessage()}};
                return effects;
            }
        }
    }

    return effects;
}

MessageEffects GoogleMessage::applyResponse(const QJsonObject &response)
{
    return applyEvent(response);
}

void GoogleMessage::handleContentDelta(const QString &text)
{
    if (m_currentBlocks.isEmpty() || !std::holds_alternative<TextContent>(m_currentBlocks.last()))
        addCurrentContent(TextContent{});

    if (auto *textContent = blockAt<TextContent>(m_currentBlocks.size() - 1))
        textContent->text += text;
}

void GoogleMessage::handleThoughtDelta(const QString &text)
{
    if (m_currentBlocks.isEmpty()
        || !std::holds_alternative<ThinkingContent>(m_currentBlocks.last()))
        addCurrentContent(ThinkingContent{});

    if (auto *thinkingContent = blockAt<ThinkingContent>(m_currentBlocks.size() - 1))
        thinkingContent->thinking += text;
}

void GoogleMessage::handleThoughtSignature(const QString &signature)
{
    const int existing = lastIndexOfBlock<ThinkingContent>();
    if (existing >= 0) {
        blockAt<ThinkingContent>(existing)->signature = signature;
        return;
    }

    const int created = addCurrentContent(ThinkingContent{});
    blockAt<ThinkingContent>(created)->signature = signature;
}

void GoogleMessage::handleToolCallStart(const QString &name)
{
    m_currentFunctionName = name;
    m_pendingFunctionArgs.clear();
}

void GoogleMessage::handleToolCallDelta(const QString &argsJson)
{
    m_pendingFunctionArgs += argsJson;
}

void GoogleMessage::handleToolCallComplete()
{
    if (m_currentFunctionName.isEmpty()) {
        return;
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    addCurrentContent(
        ToolUseContent{id, m_currentFunctionName, parseToolArguments(m_pendingFunctionArgs)});

    m_currentFunctionName.clear();
    m_pendingFunctionArgs.clear();
}

void GoogleMessage::handleStopReason(const QString &reason)
{
    static const StopReasonMap kMap{
        {QStringLiteral("STOP"), QStringLiteral("MAX_TOKENS")},
        {},
        {},
        {},
        MessageState::Complete,
        false};

    recordStopReason(reason, kMap);
}

QJsonObject GoogleMessage::toProviderFormat() const
{
    return serializeTurn(TurnRole::Assistant, m_currentBlocks);
}

QJsonObject GoogleMessage::serializeTurn(TurnRole role, const QList<TurnContent> &blocks)
{
    QJsonObject content;
    content["role"] = role == TurnRole::Assistant ? QStringLiteral("model")
                                                  : QStringLiteral("user");

    QJsonArray parts;

    QString lastThoughtSignature;

    for (const TurnContent &block : blocks) {
        std::visit(
            detail::overloaded{
                [&](const TextContent &c) { parts.append(QJsonObject{{"text", c.text}}); },
                [&](const ImageContent &c) {
                    if (c.isUrl()) {
                        parts.append(
                            QJsonObject{
                                {"fileData",
                                 QJsonObject{
                                     {"mimeType", c.mimeType},
                                     {"fileUri", c.url().toString()}}}});
                        return;
                    }
                    parts.append(
                        QJsonObject{
                            {"inlineData",
                             QJsonObject{
                                 {"mimeType",
                                  c.mimeType.isEmpty() ? QStringLiteral("image/png")
                                                       : c.mimeType},
                                 {"data", c.base64()}}}});
                },
                [&](const AudioContent &c) {
                    parts.append(
                        QJsonObject{
                            {"inlineData",
                             QJsonObject{
                                 {"mimeType",
                                  c.mimeType.isEmpty() ? QStringLiteral("audio/wav")
                                                       : c.mimeType},
                                 {"data", QString::fromUtf8(c.data.toBase64())}}}});
                },
                [&](const ToolUseContent &c) {
                    QJsonObject functionCall;
                    functionCall["name"] = c.name;
                    functionCall["args"] = c.input;

                    QJsonObject part;
                    part["functionCall"] = functionCall;
                    if (!lastThoughtSignature.isEmpty())
                        part["thoughtSignature"] = lastThoughtSignature;
                    parts.append(part);
                },
                [&](const ToolResultContent &) {},
                [&](const ThinkingContent &c) {
                    QJsonObject thinkingPart;
                    thinkingPart["text"] = c.thinking;
                    thinkingPart["thought"] = true;
                    parts.append(thinkingPart);

                    if (!c.signature.isEmpty()) {
                        lastThoughtSignature = c.signature;
                        QJsonObject signaturePart;
                        signaturePart["thoughtSignature"] = c.signature;
                        parts.append(signaturePart);
                    }
                },
                [&](const RedactedThinkingContent &) {}},
            block);
    }

    content["parts"] = parts;
    return content;
}

QJsonObject GoogleMessage::toInlineDataPart(const ToolContent &block)
{
    QString mime;
    QByteArray bytes;

    std::visit(
        detail::overloaded{
            [&](const TextContent &) {},
            [&](const ImageContent &c) {
                if (c.isUrl())
                    return;
                mime = c.mimeType.isEmpty() ? QStringLiteral("image/png") : c.mimeType;
                bytes = c.bytes();
            },
            [&](const AudioContent &c) {
                mime = c.mimeType.isEmpty() ? QStringLiteral("audio/wav") : c.mimeType;
                bytes = c.data;
            },
            [&](const ResourceContent &c) {
                if (!c.isBlob() || c.blob().isEmpty())
                    return;
                mime = c.mimeType.isEmpty() ? QStringLiteral("application/octet-stream")
                                            : c.mimeType;
                bytes = c.blob();
            },
            [&](const ResourceLinkContent &) {}},
        block);

    if (bytes.isEmpty())
        return QJsonObject{};

    return QJsonObject{
        {"inlineData",
         QJsonObject{
             {"mimeType", mime},
             {"data", QString::fromUtf8(bytes.toBase64())},
         }},
    };
}

namespace {

QString buildGeminiResponseText(const ToolResult &r)
{
    QStringList chunks;
    for (const ToolContent &block : r.content) {
        std::visit(
            detail::overloaded{
                [&](const TextContent &c) {
                    if (!c.text.isEmpty())
                        chunks.append(c.text);
                },
                [&](const ImageContent &) {},
                [&](const AudioContent &) {},
                [&](const ResourceContent &c) {
                    if (!c.isBlob() && !c.text().isEmpty())
                        chunks.append(c.text());
                },
                [&](const ResourceLinkContent &c) {
                    chunks.append(QString("[resource link: %1]").arg(c.uri));
                }},
            block);
    }
    return chunks.join('\n');
}

} // namespace

QJsonArray GoogleMessage::createToolResultParts(
    const QHash<QString, ToolResult> &toolResults) const
{
    return mapToolResults(
        toolResults, [](const ToolUseContent &use, const ToolResult &r, QJsonArray &parts) {
        QJsonObject functionResponse;
        functionResponse["name"] = use.name;

        if (r.hasOnlyText()) {
            functionResponse["response"] = QJsonObject{{"result", toolResultText(r)}};
            parts.append(QJsonObject{{"functionResponse", functionResponse}});
            return;
        }

        functionResponse["response"] = QJsonObject{{"result", buildGeminiResponseText(r)}};

        QJsonArray media;
        for (const ToolContent &block : r.content) {
            const QJsonObject inlinePart = toInlineDataPart(block);
            if (!inlinePart.isEmpty())
                media.append(inlinePart);
        }
        if (!media.isEmpty())
            functionResponse["parts"] = media;

        parts.append(QJsonObject{{"functionResponse", functionResponse}});
        });
}

QString GoogleMessage::toolResultTurnRole(const QJsonArray &parts)
{
    for (const QJsonValue &part : parts) {
        if (part.toObject().value("functionResponse").toObject().contains("parts"))
            return QStringLiteral("user");
    }
    return QStringLiteral("function");
}

void GoogleMessage::clearDerivedCaches()
{
    m_pendingFunctionArgs.clear();
    m_currentFunctionName.clear();
}

bool GoogleMessage::isErrorFinishReason() const
{
    const QString reason = stopReason();
    return reason == "SAFETY" || reason == "RECITATION" || reason == "MALFORMED_FUNCTION_CALL"
           || reason == "PROHIBITED_CONTENT" || reason == "SPII" || reason == "OTHER";
}

QString GoogleMessage::errorFinishMessage() const
{
    const QString reason = stopReason();
    if (reason == "SAFETY") {
        return "Response blocked by safety filters";
    } else if (reason == "RECITATION") {
        return "Response blocked due to recitation of copyrighted content";
    } else if (reason == "MALFORMED_FUNCTION_CALL") {
        return "Model attempted to call a function with malformed arguments. Please try rephrasing "
               "your request or disabling tools.";
    } else if (reason == "PROHIBITED_CONTENT") {
        return "Response blocked due to prohibited content";
    } else if (reason == "SPII") {
        return "Response blocked due to sensitive personally identifiable information";
    } else if (reason == "OTHER") {
        return "Request failed due to an unknown reason";
    }
    return QString();
}

} // namespace LLMQore
