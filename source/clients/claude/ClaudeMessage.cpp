// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "ClaudeMessage.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <LLMCore/Log.hpp>

namespace LLMCore {

ClaudeMessage::ClaudeMessage(QObject *parent)
    : BaseMessage(parent)
{}

void ClaudeMessage::handleContentBlockStart(
    int index, const QString &blockType, const QJsonObject &data)
{
    qCDebug(llmClaudeLog).noquote()
        << QString("handleContentBlockStart index=%1, blockType=%2").arg(index).arg(blockType);

    if (blockType == "text") {
        addCurrentContent<TextContent>();

    } else if (blockType == "image") {
        QJsonObject source = data["source"].toObject();
        QString sourceType = source["type"].toString();
        QString imageData;
        QString mediaType;
        ImageContent::ImageSourceType imgSourceType = ImageContent::ImageSourceType::Base64;

        if (sourceType == "base64") {
            imageData = source["data"].toString();
            mediaType = source["media_type"].toString();
            imgSourceType = ImageContent::ImageSourceType::Base64;
        } else if (sourceType == "url") {
            imageData = source["url"].toString();
            imgSourceType = ImageContent::ImageSourceType::Url;
        }

        addCurrentContent<ImageContent>(imageData, mediaType, imgSourceType);

    } else if (blockType == "tool_use") {
        QString toolId = data["id"].toString();
        QString toolName = data["name"].toString();
        QJsonObject toolInput = data["input"].toObject();

        addCurrentContent<ToolUseContent>(toolId, toolName, toolInput);
        m_pendingToolInputs[index] = "";

    } else if (blockType == "thinking") {
        QString thinking = data["thinking"].toString();
        QString signature = data["signature"].toString();
        qCDebug(llmClaudeLog).noquote()
            << QString("Creating thinking block with signature length=%1").arg(signature.length());
        addCurrentContent<ThinkingContent>(thinking, signature);

    } else if (blockType == "redacted_thinking") {
        QString signature = data["signature"].toString();
        qCDebug(llmClaudeLog).noquote()
            << QString("Creating redacted_thinking block with signature length=%1")
                   .arg(signature.length());
        addCurrentContent<RedactedThinkingContent>(signature);
    }
}

void ClaudeMessage::handleContentBlockDelta(
    int index, const QString &deltaType, const QJsonObject &delta)
{
    if (index >= m_currentBlocks.size()) {
        return;
    }

    if (deltaType == "text_delta") {
        if (auto textContent = dynamic_cast<TextContent *>(m_currentBlocks[index])) {
            textContent->appendText(delta["text"].toString());
        }

    } else if (deltaType == "input_json_delta") {
        QString partialJson = delta["partial_json"].toString();
        if (m_pendingToolInputs.contains(index)) {
            m_pendingToolInputs[index] += partialJson;
        }

    } else if (deltaType == "thinking_delta") {
        if (auto thinkingContent = dynamic_cast<ThinkingContent *>(m_currentBlocks[index])) {
            thinkingContent->appendThinking(delta["thinking"].toString());
        }

    } else if (deltaType == "signature_delta") {
        if (auto thinkingContent = dynamic_cast<ThinkingContent *>(m_currentBlocks[index])) {
            QString signature = delta["signature"].toString();
            thinkingContent->setSignature(signature);
            qCDebug(llmClaudeLog).noquote()
                << QString("Set signature for thinking block %1: length=%2")
                       .arg(index)
                       .arg(signature.length());
        } else if (
            auto redactedContent = dynamic_cast<RedactedThinkingContent *>(m_currentBlocks[index])) {
            QString signature = delta["signature"].toString();
            redactedContent->setSignature(signature);
            qCDebug(llmClaudeLog).noquote()
                << QString("Set signature for redacted_thinking block %1: length=%2")
                       .arg(index)
                       .arg(signature.length());
        }
    }
}

void ClaudeMessage::handleContentBlockStop(int index)
{
    if (m_pendingToolInputs.contains(index)) {
        QString jsonInput = m_pendingToolInputs[index];
        QJsonObject inputObject;

        if (!jsonInput.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonInput.toUtf8());
            if (doc.isObject()) {
                inputObject = doc.object();
            }
        }

        if (index < m_currentBlocks.size()) {
            if (auto toolContent = dynamic_cast<ToolUseContent *>(m_currentBlocks[index])) {
                toolContent->setInput(inputObject);
            }
        }

        m_pendingToolInputs.remove(index);
    }
}

void ClaudeMessage::handleStopReason(const QString &stopReason)
{
    m_stopReason = stopReason;
    updateStateFromStopReason();
}

static QJsonValue serializeBlockClaude(const ContentBlock *block)
{
    if (const auto *text = dynamic_cast<const TextContent *>(block)) {
        return QJsonObject{{"type", "text"}, {"text", text->text()}};
    }
    if (const auto *image = dynamic_cast<const ImageContent *>(block)) {
        QJsonObject source;
        if (image->sourceType() == ImageContent::ImageSourceType::Base64) {
            source["type"] = "base64";
            source["media_type"] = image->mediaType();
            source["data"] = image->data();
        } else {
            source["type"] = "url";
            source["url"] = image->data();
        }
        return QJsonObject{{"type", "image"}, {"source", source}};
    }
    if (const auto *tool = dynamic_cast<const ToolUseContent *>(block)) {
        return QJsonObject{
            {"type", "tool_use"},
            {"id", tool->id()},
            {"name", tool->name()},
            {"input", tool->input()}};
    }
    if (const auto *thinking = dynamic_cast<const ThinkingContent *>(block)) {
        QJsonObject obj{{"type", "thinking"}, {"thinking", thinking->thinking()}};
        if (!thinking->signature().isEmpty()) {
            obj["signature"] = thinking->signature();
        }
        return obj;
    }
    if (const auto *redacted = dynamic_cast<const RedactedThinkingContent *>(block)) {
        QJsonObject obj{{"type", "redacted_thinking"}};
        if (!redacted->signature().isEmpty()) {
            obj["signature"] = redacted->signature();
        }
        return obj;
    }
    return {};
}

QJsonObject ClaudeMessage::toProviderFormat() const
{
    QJsonObject message;
    message["role"] = "assistant";

    QJsonArray content;

    for (const auto *block : m_currentBlocks) {
        content.append(serializeBlockClaude(block));
    }

    message["content"] = content;

    qCDebug(llmClaudeLog).noquote()
        << QString("toProviderFormat - message with %1 content block(s)").arg(m_currentBlocks.size());

    return message;
}

namespace {

QJsonObject toClaudeInnerBlock(const ToolContent &block)
{
    switch (block.type) {
    case ToolContent::Text:
        return QJsonObject{{"type", "text"}, {"text", block.text}};
    case ToolContent::Image: {
        const QString mime = block.mimeType.isEmpty() ? QStringLiteral("image/png")
                                                      : block.mimeType;
        return QJsonObject{
            {"type", "image"},
            {"source",
             QJsonObject{
                 {"type", "base64"},
                 {"media_type", mime},
                 {"data", QString::fromUtf8(block.data.toBase64())}}}};
    }
    case ToolContent::Audio:
        return QJsonObject{
            {"type", "text"},
            {"text",
             QString("[audio: %1]")
                 .arg(block.mimeType.isEmpty() ? QStringLiteral("unknown") : block.mimeType)}};
    case ToolContent::Resource:
        if (!block.resourceText.isEmpty())
            return QJsonObject{{"type", "text"}, {"text", block.resourceText}};
        return QJsonObject{
            {"type", "text"}, {"text", QString("[resource: %1]").arg(block.uri)}};
    case ToolContent::ResourceLink:
        return QJsonObject{
            {"type", "text"}, {"text", QString("[resource link: %1]").arg(block.uri)}};
    }
    return QJsonObject{{"type", "text"}, {"text", QString()}};
}

QJsonValue buildClaudeToolResultContent(const ToolResult &r)
{
    if (r.content.isEmpty())
        return QString();
    if (r.content.size() == 1 && r.content.first().type == ToolContent::Text)
        return r.content.first().text;

    QJsonArray arr;
    for (const ToolContent &block : r.content)
        arr.append(toClaudeInnerBlock(block));
    return arr;
}

} // namespace

QJsonArray ClaudeMessage::createToolResultsContent(
    const QHash<QString, ToolResult> &toolResults) const
{
    QJsonArray results;

    for (const auto *toolContent : getCurrentToolUseContent()) {
        if (!toolResults.contains(toolContent->id()))
            continue;

        const ToolResult &r = toolResults[toolContent->id()];
        QJsonObject block{
            {"type", "tool_result"},
            {"tool_use_id", toolContent->id()},
            {"content", buildClaudeToolResultContent(r)},
        };
        if (r.isError)
            block.insert("is_error", true);
        results.append(block);
    }

    return results;
}

QList<RedactedThinkingContent *> ClaudeMessage::getCurrentRedactedThinkingContent() const
{
    QList<RedactedThinkingContent *> redactedBlocks;
    for (auto *block : m_currentBlocks) {
        if (auto *redactedContent = dynamic_cast<RedactedThinkingContent *>(block)) {
            redactedBlocks.append(redactedContent);
        }
    }
    return redactedBlocks;
}

void ClaudeMessage::startNewContinuation()
{
    qCDebug(llmClaudeLog).noquote() << "Starting new continuation";

    BaseMessage::startNewContinuation();
    m_pendingToolInputs.clear();
    m_stopReason.clear();
}

void ClaudeMessage::updateStateFromStopReason()
{
    if (m_stopReason == "tool_use" && !getCurrentToolUseContent().empty()) {
        m_state = MessageState::RequiresToolExecution;
    } else if (m_stopReason == "end_turn") {
        m_state = MessageState::Final;
    } else {
        m_state = MessageState::Complete;
    }
}

} // namespace LLMCore
