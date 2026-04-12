// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include "GoogleMessage.hpp"

#include <QJsonDocument>
#include <QStringList>
#include <QUuid>

#include <LLMCore/Log.hpp>

namespace LLMCore {

GoogleMessage::GoogleMessage(QObject *parent)
    : BaseMessage(parent)
{}

void GoogleMessage::handleContentDelta(const QString &text)
{
    if (m_currentBlocks.isEmpty() || !dynamic_cast<TextContent *>(m_currentBlocks.last())) {
        addCurrentContent<TextContent>();
    }

    if (auto textContent = dynamic_cast<TextContent *>(m_currentBlocks.last())) {
        textContent->appendText(text);
    }
}

void GoogleMessage::handleThoughtDelta(const QString &text)
{
    if (m_currentBlocks.isEmpty() || !dynamic_cast<ThinkingContent *>(m_currentBlocks.last())) {
        addCurrentContent<ThinkingContent>();
    }

    if (auto thinkingContent = dynamic_cast<ThinkingContent *>(m_currentBlocks.last())) {
        thinkingContent->appendThinking(text);
    }
}

void GoogleMessage::handleThoughtSignature(const QString &signature)
{
    for (int i = m_currentBlocks.size() - 1; i >= 0; --i) {
        if (auto thinkingContent = dynamic_cast<ThinkingContent *>(m_currentBlocks[i])) {
            thinkingContent->setSignature(signature);
            return;
        }
    }

    auto thinkingContent = addCurrentContent<ThinkingContent>();
    thinkingContent->setSignature(signature);
}

void GoogleMessage::handleFunctionCallStart(const QString &name)
{
    m_currentFunctionName = name;
    m_pendingFunctionArgs.clear();
}

void GoogleMessage::handleFunctionCallArgsDelta(const QString &argsJson)
{
    m_pendingFunctionArgs += argsJson;
}

void GoogleMessage::handleFunctionCallComplete()
{
    if (m_currentFunctionName.isEmpty()) {
        return;
    }

    QJsonObject args;
    if (!m_pendingFunctionArgs.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(m_pendingFunctionArgs.toUtf8());
        if (doc.isObject()) {
            args = doc.object();
        }
    }

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    addCurrentContent<ToolUseContent>(id, m_currentFunctionName, args);

    m_currentFunctionName.clear();
    m_pendingFunctionArgs.clear();
}

void GoogleMessage::handleFinishReason(const QString &reason)
{
    m_finishReason = reason;
    updateStateFromFinishReason();
}

QJsonObject GoogleMessage::toProviderFormat() const
{
    QJsonObject content;
    content["role"] = "model";

    QJsonArray parts;

    QString lastThoughtSignature;

    for (const auto *block : m_currentBlocks) {
        if (!block)
            continue;

        if (const auto *text = dynamic_cast<const TextContent *>(block)) {
            parts.append(QJsonObject{{"text", text->text()}});
        } else if (const auto *tool = dynamic_cast<const ToolUseContent *>(block)) {
            QJsonObject functionCall;
            functionCall["name"] = tool->name();
            functionCall["args"] = tool->input();

            QJsonObject part;
            part["functionCall"] = functionCall;
            if (!lastThoughtSignature.isEmpty())
                part["thoughtSignature"] = lastThoughtSignature;
            parts.append(part);
        } else if (const auto *thinking = dynamic_cast<const ThinkingContent *>(block)) {
            QJsonObject thinkingPart;
            thinkingPart["text"] = thinking->thinking();
            thinkingPart["thought"] = true;
            parts.append(thinkingPart);

            if (!thinking->signature().isEmpty()) {
                lastThoughtSignature = thinking->signature();
                QJsonObject signaturePart;
                signaturePart["thoughtSignature"] = thinking->signature();
                parts.append(signaturePart);
            }
        }
    }

    content["parts"] = parts;
    return content;
}

namespace {

QJsonObject toInlineDataPart(const ToolContent &block)
{
    QString mime;
    QByteArray bytes;
    switch (block.type) {
    case ToolContent::Image:
        mime = block.mimeType.isEmpty() ? QStringLiteral("image/png") : block.mimeType;
        bytes = block.data;
        break;
    case ToolContent::Audio:
        mime = block.mimeType.isEmpty() ? QStringLiteral("audio/wav") : block.mimeType;
        bytes = block.data;
        break;
    case ToolContent::Resource:
        if (!block.resourceBlob.isEmpty()) {
            mime = block.mimeType.isEmpty() ? QStringLiteral("application/octet-stream")
                                            : block.mimeType;
            bytes = block.resourceBlob;
        }
        break;
    default:
        break;
    }
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

QString buildGeminiResponseText(const ToolResult &r)
{
    QStringList chunks;
    for (const ToolContent &block : r.content) {
        switch (block.type) {
        case ToolContent::Text:
            if (!block.text.isEmpty())
                chunks.append(block.text);
            break;
        case ToolContent::Resource:
            if (!block.resourceText.isEmpty())
                chunks.append(block.resourceText);
            break;
        case ToolContent::ResourceLink:
            chunks.append(QString("[resource link: %1]").arg(block.uri));
            break;
        case ToolContent::Image:
        case ToolContent::Audio:
            break;
        }
    }
    return chunks.join('\n');
}

bool hasOnlyText(const ToolResult &r)
{
    for (const ToolContent &b : r.content) {
        if (b.type != ToolContent::Text)
            return false;
    }
    return true;
}

} // namespace

QJsonArray GoogleMessage::createToolResultParts(
    const QHash<QString, ToolResult> &toolResults) const
{
    QJsonArray parts;

    for (const auto *toolContent : getCurrentToolUseContent()) {
        if (!toolResults.contains(toolContent->id()))
            continue;

        const ToolResult &r = toolResults[toolContent->id()];
        QJsonObject functionResponse;
        functionResponse["name"] = toolContent->name();

        if (hasOnlyText(r)) {
            functionResponse["response"] = QJsonObject{{"result", r.asText()}};
        } else {
            // Textual preamble (if any) + inline binary parts.
            const QString textPart = buildGeminiResponseText(r);
            functionResponse["response"]
                = QJsonObject{{"result", textPart.isEmpty() ? QString() : textPart}};

            QJsonArray innerParts;
            for (const ToolContent &block : r.content) {
                const QJsonObject inlinePart = toInlineDataPart(block);
                if (!inlinePart.isEmpty())
                    innerParts.append(inlinePart);
            }
            if (!innerParts.isEmpty())
                functionResponse["parts"] = innerParts;
        }

        parts.append(QJsonObject{{"functionResponse", functionResponse}});
    }

    return parts;
}

void GoogleMessage::startNewContinuation()
{
    qCDebug(llmGoogleLog).noquote() << "Starting new continuation";

    BaseMessage::startNewContinuation();
    m_pendingFunctionArgs.clear();
    m_currentFunctionName.clear();
    m_finishReason.clear();
}

bool GoogleMessage::isErrorFinishReason() const
{
    return m_finishReason == "SAFETY" || m_finishReason == "RECITATION"
           || m_finishReason == "MALFORMED_FUNCTION_CALL" || m_finishReason == "PROHIBITED_CONTENT"
           || m_finishReason == "SPII" || m_finishReason == "OTHER";
}

QString GoogleMessage::getErrorMessage() const
{
    if (m_finishReason == "SAFETY") {
        return "Response blocked by safety filters";
    } else if (m_finishReason == "RECITATION") {
        return "Response blocked due to recitation of copyrighted content";
    } else if (m_finishReason == "MALFORMED_FUNCTION_CALL") {
        return "Model attempted to call a function with malformed arguments. Please try rephrasing "
               "your request or disabling tools.";
    } else if (m_finishReason == "PROHIBITED_CONTENT") {
        return "Response blocked due to prohibited content";
    } else if (m_finishReason == "SPII") {
        return "Response blocked due to sensitive personally identifiable information";
    } else if (m_finishReason == "OTHER") {
        return "Request failed due to an unknown reason";
    }
    return QString();
}

void GoogleMessage::updateStateFromFinishReason()
{
    if (m_finishReason == "STOP" || m_finishReason == "MAX_TOKENS") {
        m_state = getCurrentToolUseContent().isEmpty() ? MessageState::Complete
                                                       : MessageState::RequiresToolExecution;
    } else {
        m_state = MessageState::Complete;
    }
}

} // namespace LLMCore
