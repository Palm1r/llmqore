// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/BaseTool.hpp>
#include "OpenAIResponsesMessage.hpp"

#include <LLMQore/Log.hpp>

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>

namespace LLMQore {

namespace {

class OpenAIResponsesToolDialect : public ToolDialect
{
public:
    QJsonObject wrapDefinition(const BaseTool &tool) const override
    {
        return QJsonObject{
            {"type", "function"},
            {"name", tool.id()},
            {"description", tool.description()},
            {"parameters", tool.parametersSchema()}};
    }
};

} // namespace

const ToolDialect &OpenAIResponsesMessage::toolDialect()
{
    static const OpenAIResponsesToolDialect dialect;
    return dialect;
}


OpenAIResponsesMessage::OpenAIResponsesMessage(QObject *parent)
    : BaseMessage(parent)
{}

void OpenAIResponsesMessage::handleContentDelta(const QString &text)
{
    if (!text.isEmpty()) {
        const int index = getOrCreateTextItemIndex();
        if (auto *textItem = blockAt<TextContent>(index))
            textItem->text += text;
    }
}

void OpenAIResponsesMessage::handleToolCallStart(const QString &callId, const QString &name)
{
    m_toolCalls[callId] = addCurrentContent(ToolUseContent{callId, name, {}});
    m_pendingToolArguments[callId] = "";
}

void OpenAIResponsesMessage::handleToolCallDelta(const QString &callId, const QString &argumentsDelta)
{
    if (m_pendingToolArguments.contains(callId)) {
        m_pendingToolArguments[callId] += argumentsDelta;
    }
}

void OpenAIResponsesMessage::handleToolCallComplete(
    const QString &callId, const QString &finalArguments)
{
    if (m_pendingToolArguments.contains(callId) && m_toolCalls.contains(callId)) {
        const QString jsonArgs = !finalArguments.isEmpty() ? finalArguments
                                                           : m_pendingToolArguments[callId];
        QJsonObject argsObject;

        if (!jsonArgs.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonArgs.toUtf8());
            if (doc.isObject()) {
                argsObject = doc.object();
            }
        }

        if (auto *toolContent = blockAt<ToolUseContent>(m_toolCalls.value(callId, -1)))
            toolContent->input = argsObject;
        m_pendingToolArguments.remove(callId);
    }
}

void OpenAIResponsesMessage::handleReasoningStart(const QString &itemId)
{
    ThinkingContent content;
    content.itemId = itemId;
    m_thinkingBlocks[itemId] = addCurrentContent(std::move(content));
}

void OpenAIResponsesMessage::handleReasoningEncryptedContent(
    const QString &itemId, const QString &encryptedContent)
{
    if (encryptedContent.isEmpty())
        return;

    if (!m_thinkingBlocks.contains(itemId))
        handleReasoningStart(itemId);

    if (auto *thinking = blockAt<ThinkingContent>(m_thinkingBlocks.value(itemId, -1)))
        thinking->encryptedContent = encryptedContent;
}

void OpenAIResponsesMessage::handleReasoningDelta(const QString &itemId, const QString &text)
{
    if (text.isEmpty())
        return;

    if (!m_thinkingBlocks.contains(itemId))
        handleReasoningStart(itemId);

    if (auto *thinking = blockAt<ThinkingContent>(m_thinkingBlocks.value(itemId, -1)))
        thinking->thinking += text;
}

void OpenAIResponsesMessage::handleStatus(const QString &status)
{
    m_status = status;
    updateStateFromStatus();
}

namespace {

const QLatin1String kStreamingReasoningPlaceholder(
    "[Reasoning process completed, but detailed thinking is not available in streaming mode.]");

} // namespace

MessageEffects OpenAIResponsesMessage::applyEvent(const QString &eventType, const QJsonObject &data)
{
    MessageEffects effects;

    if (eventType == "response.output_text.delta") {
        const QString delta = data["delta"].toString();
        if (!delta.isEmpty()) {
            handleContentDelta(delta);
            effects.chunk = delta;
        }

    } else if (eventType == "response.output_text.done") {
        effects.fullText = data["text"].toString();

    } else if (eventType == "response.output_item.added") {
        const QJsonObject item = data["item"].toObject();
        const QString itemType = item["type"].toString();

        if (itemType == "function_call") {
            const QString callId = item["call_id"].toString();
            const QString name = item["name"].toString();
            if (!callId.isEmpty() && !name.isEmpty()) {
                m_itemIdToCallId[item["id"].toString()] = callId;
                handleToolCallStart(callId, name);
            }
        } else if (itemType == "reasoning") {
            const QString itemId = item["id"].toString();
            if (!itemId.isEmpty()) {
                handleReasoningStart(itemId);
                handleReasoningEncryptedContent(itemId, item["encrypted_content"].toString());
            }
        }

    } else if (eventType == "response.reasoning_content.delta") {
        handleReasoningDelta(data["item_id"].toString(), data["delta"].toString());

    } else if (eventType == "response.reasoning_content.done") {
        if (!data["item_id"].toString().isEmpty())
            effects.thinkingCompleted = true;

    } else if (eventType == "response.function_call_arguments.delta") {
        const QString callId = m_itemIdToCallId.value(data["item_id"].toString());
        const QString delta = data["delta"].toString();
        if (!callId.isEmpty() && !delta.isEmpty())
            handleToolCallDelta(callId, delta);

    } else if (
        eventType == "response.function_call_arguments.done"
        || eventType == "response.output_item.done") {
        applyItemDone(data, effects);

    } else if (eventType == "response.completed") {
        applyTerminal(data["response"].toObject(), {}, effects);

    } else if (eventType == "response.incomplete") {
        applyTerminal(data["response"].toObject(), QStringLiteral("incomplete"), effects);
    }

    return effects;
}

MessageEffects OpenAIResponsesMessage::applyResponse(const QJsonObject &response)
{
    MessageEffects effects;

    const QJsonArray output = response["output"].toArray();
    for (const QJsonValue &item : output)
        applyOutputItem(item.toObject(), effects);

    effects.thinkingCompleted = true;

    const QString status = response["status"].toString();
    if (!status.isEmpty()) {
        handleStatus(status);
        effects.toolsReady = true;
    }

    effects.usage = response;
    return effects;
}

void OpenAIResponsesMessage::applyOutputItem(const QJsonObject &item, MessageEffects &effects)
{
    const QString itemType = item["type"].toString();

    if (itemType == "reasoning") {
        applyReasoningItem(item["id"].toString(), item, {});

    } else if (itemType == "message") {
        const QJsonArray content = item["content"].toArray();
        for (const QJsonValue &part : content) {
            const QJsonObject partObject = part.toObject();
            if (partObject["type"].toString() != QLatin1String("output_text"))
                continue;
            const QString text = partObject["text"].toString();
            if (text.isEmpty())
                continue;
            handleContentDelta(text);
            effects.chunk += text;
        }

    } else if (itemType == "function_call") {
        const QString callId = item["call_id"].toString();
        const QString name = item["name"].toString();
        if (!callId.isEmpty() && !name.isEmpty()) {
            handleToolCallStart(callId, name);
            handleToolCallComplete(callId, item["arguments"].toString());
        }
    }
}

void OpenAIResponsesMessage::applyItemDone(const QJsonObject &data, MessageEffects &effects)
{
    const QString itemId = data["item_id"].toString();
    const QJsonObject item = data["item"].toObject();
    const QString itemType = item["type"].toString();

    if (!item.isEmpty() && itemType == "reasoning") {
        const QString finalItemId = itemId.isEmpty() ? item["id"].toString() : itemId;
        applyReasoningItem(finalItemId, item, kStreamingReasoningPlaceholder);
        if (!finalItemId.isEmpty())
            effects.thinkingCompleted = true;

    } else if (item.isEmpty() && !itemId.isEmpty()) {
        const QString callId = m_itemIdToCallId.value(itemId);
        if (!callId.isEmpty())
            handleToolCallComplete(callId, data["arguments"].toString());

    } else if (!item.isEmpty() && itemType == "function_call") {
        const QString callId = item["call_id"].toString();
        if (!callId.isEmpty())
            handleToolCallComplete(callId, item["arguments"].toString());
    }
}

void OpenAIResponsesMessage::applyReasoningItem(
    const QString &itemId, const QJsonObject &item, const QString &placeholder)
{
    if (itemId.isEmpty())
        return;

    QString text = reasoningTextOf(item);
    if (text.isEmpty())
        text = placeholder;

    const QString encrypted = item["encrypted_content"].toString();
    if (text.isEmpty() && encrypted.isEmpty())
        return;

    handleReasoningDelta(itemId, text);
    handleReasoningEncryptedContent(itemId, encrypted);
}

void OpenAIResponsesMessage::applyTerminal(
    const QJsonObject &response, const QString &fallbackStatus, MessageEffects &effects)
{
    if (response.isEmpty()) {
        handleStatus(fallbackStatus);
    } else {
        effects.fallbackText = aggregatedTextOf(response);
        handleStatus(response["status"].toString());
        effects.usage = response;
    }

    effects.thinkingCompleted = true;
    effects.toolsReady = true;
}

QString OpenAIResponsesMessage::aggregatedTextOf(const QJsonObject &response)
{
    if (response.contains("output_text")) {
        const QString outputText = response["output_text"].toString();
        if (!outputText.isEmpty())
            return outputText;
    }

    QString aggregated;
    const QJsonArray output = response["output"].toArray();
    for (const QJsonValue &item : output) {
        const QJsonObject itemObject = item.toObject();
        if (itemObject["type"].toString() != QLatin1String("message"))
            continue;
        const QJsonArray content = itemObject["content"].toArray();
        for (const QJsonValue &part : content) {
            const QJsonObject partObject = part.toObject();
            if (partObject["type"].toString() == QLatin1String("output_text"))
                aggregated += partObject["text"].toString();
        }
    }
    return aggregated;
}

QString OpenAIResponsesMessage::reasoningTextOf(const QJsonObject &item)
{
    const QJsonArray summary = item["summary"].toArray();
    for (const QJsonValue &entry : summary) {
        const QJsonObject entryObject = entry.toObject();
        if (entryObject["type"].toString() == QLatin1String("summary_text"))
            return entryObject["text"].toString();
    }

    QStringList texts;
    const QJsonArray content = item["content"].toArray();
    for (const QJsonValue &entry : content) {
        const QJsonObject entryObject = entry.toObject();
        if (entryObject["type"].toString() == QLatin1String("reasoning_text"))
            texts.append(entryObject["text"].toString());
    }
    return texts.join(QLatin1Char('\n'));
}

QList<QJsonObject> OpenAIResponsesMessage::toItemsFormat(ReasoningPersistence reasoning) const
{
    return serializeTurn(TurnRole::Assistant, m_currentBlocks, reasoning);
}

QList<QJsonObject> OpenAIResponsesMessage::serializeTurn(
    TurnRole role, const QList<TurnContent> &blocks, ReasoningPersistence reasoning)
{
    const bool isAssistant = role == TurnRole::Assistant;
    const bool includeReasoning = reasoning == ReasoningPersistence::Replay;

    QList<QJsonObject> items;
    QJsonArray parts;

    QString textContent;
    int textPosition = -1;

    for (const TurnContent &block : blocks) {
        std::visit(
            detail::overloaded{
                [&](const TextContent &c) {
                    if (!isAssistant) {
                        parts.append(QJsonObject{{"type", "input_text"}, {"text", c.text}});
                        return;
                    }
                    if (textPosition < 0)
                        textPosition = items.size();
                    textContent += c.text;
                },
                [&](const ImageContent &c) {
                    if (isAssistant)
                        return;
                    const QString mime = c.mimeType.isEmpty() ? QStringLiteral("image/png")
                                                              : c.mimeType;
                    const QString url = c.isUrl()
                        ? c.url().toString()
                        : QStringLiteral("data:%1;base64,%2").arg(mime, c.base64());
                    parts.append(
                        QJsonObject{
                            {"type", "input_image"}, {"image_url", url}, {"detail", "auto"}});
                },
                [&](const AudioContent &) {},
                [&](const ToolUseContent &c) {
                    QJsonObject functionCallItem;
                    functionCallItem["type"] = "function_call";
                    functionCallItem["call_id"] = c.id;
                    functionCallItem["name"] = c.name;
                    functionCallItem["arguments"] = QString::fromUtf8(
                        QJsonDocument(c.input).toJson(QJsonDocument::Compact));
                    items.append(functionCallItem);
                },
                [&](const ToolResultContent &) {},
                [&](const ThinkingContent &c) {
                    if (!includeReasoning || c.itemId.isEmpty()
                        || c.encryptedContent.isEmpty())
                        return;

                    QJsonObject reasoningItem;
                    reasoningItem["type"] = "reasoning";
                    reasoningItem["id"] = c.itemId;
                    reasoningItem["encrypted_content"] = c.encryptedContent;
                    reasoningItem["summary"] = QJsonArray{};
                    items.append(reasoningItem);
                },
                [&](const RedactedThinkingContent &) {}},
            block);
    }

    if (!isAssistant) {
        if (!parts.isEmpty())
            items.append(QJsonObject{{"role", "user"}, {"content", parts}});
        return items;
    }

    if (!textContent.isEmpty()) {
        QJsonObject message;
        message["role"] = "assistant";
        message["content"] = textContent;
        items.insert(std::clamp(textPosition, 0, int(items.size())), message);
    }

    return items;
}

QJsonObject OpenAIResponsesMessage::toResponsesInnerBlock(const ToolContent &block)
{
    return std::visit(
        detail::overloaded{
            [](const TextContent &c) -> QJsonObject {
                return QJsonObject{{"type", "input_text"}, {"text", c.text}};
            },
            [](const ImageContent &c) -> QJsonObject {
                if (c.isUrl()) {
                    return QJsonObject{
                        {"type", "input_image"},
                        {"image_url", c.url().toString()},
                        {"detail", "auto"}};
                }
                const QString mime = c.mimeType.isEmpty() ? QStringLiteral("image/png")
                                                          : c.mimeType;
                return QJsonObject{
                    {"type", "input_image"},
                    {"image_url", QStringLiteral("data:%1;base64,%2").arg(mime, c.base64())},
                    {"detail", "auto"}};
            },
            [](const AudioContent &c) -> QJsonObject {
                return QJsonObject{
                    {"type", "input_text"},
                    {"text",
                     QString("[audio: %1]")
                         .arg(c.mimeType.isEmpty() ? QStringLiteral("unknown") : c.mimeType)}};
            },
            [](const ResourceContent &c) -> QJsonObject {
                if (!c.isBlob() && !c.text().isEmpty())
                    return QJsonObject{{"type", "input_text"}, {"text", c.text()}};
                return QJsonObject{
                    {"type", "input_text"}, {"text", QString("[resource: %1]").arg(c.uri)}};
            },
            [](const ResourceLinkContent &c) -> QJsonObject {
                return QJsonObject{
                    {"type", "input_text"}, {"text", QString("[resource link: %1]").arg(c.uri)}};
            }},
        block);
}


QJsonArray OpenAIResponsesMessage::createToolResultItems(
    const QHash<QString, ToolResult> &toolResults) const
{
    return mapToolResults(
        toolResults, [](const ToolUseContent &use, const ToolResult &r, QJsonArray &out) {
            QJsonObject item;
            item["type"] = "function_call_output";
            item["call_id"] = use.id;

            if (r.hasOnlyText()) {
                item["output"] = toolResultText(r);
            } else {
                QJsonArray blocks;
                for (const ToolContent &block : r.content)
                    blocks.append(toResponsesInnerBlock(block));
                item["output"] = blocks;
            }

            out.append(item);
        });
}

QString OpenAIResponsesMessage::accumulatedText() const
{
    QString text;
    for (const TurnContent &block : m_currentBlocks) {
        if (const auto *textContent = std::get_if<TextContent>(&block))
            text += textContent->text;
    }
    return text;
}

void OpenAIResponsesMessage::updateStateFromStatus()
{
    if (m_status == "completed") {
        if (!currentToolUseContent().isEmpty()) {
            m_state = MessageState::RequiresToolExecution;
        } else {
            m_state = MessageState::Complete;
        }
    } else if (m_status == "in_progress") {
        m_state = MessageState::Building;
    } else if (m_status == "failed" || m_status == "cancelled" || m_status == "incomplete") {
        m_state = MessageState::Final;
    } else {
        m_state = MessageState::Building;
    }
}

int OpenAIResponsesMessage::getOrCreateTextItemIndex()
{
    return getOrCreateTextContentIndex();
}

void OpenAIResponsesMessage::startNewContinuation()
{
    m_toolCalls.clear();
    m_thinkingBlocks.clear();
    m_itemIdToCallId.clear();

    BaseMessage::startNewContinuation();

    m_pendingToolArguments.clear();
    m_status.clear();
}

} // namespace LLMQore
