// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/AcpTypes.hpp>
#include <LLMQore/JsonFields.hpp>


namespace LLMQore::Acp {

using LLMQore::Json::field;
using LLMQore::Json::nullWhenEmpty;
using LLMQore::Json::omitEmpty;

constexpr auto jsonSchema(const EnvVariable *)
{
    return std::make_tuple(
        field("name", &EnvVariable::name), field("value", &EnvVariable::value));
}

constexpr auto jsonSchema(const Implementation *)
{
    return std::make_tuple(
        field("name", &Implementation::name),
        field("version", &Implementation::version),
        omitEmpty("title", &Implementation::title));
}

constexpr auto jsonSchema(const FileSystemCapability *)
{
    return std::make_tuple(
        field("readTextFile", &FileSystemCapability::readTextFile),
        field("writeTextFile", &FileSystemCapability::writeTextFile));
}

constexpr auto jsonSchema(const ClientCapabilities *)
{
    return std::make_tuple(
        field("fs", &ClientCapabilities::fs), field("terminal", &ClientCapabilities::terminal));
}

constexpr auto jsonExtras(const ClientCapabilities *)
{
    return &ClientCapabilities::extras;
}

constexpr auto jsonSchema(const PromptCapabilities *)
{
    return std::make_tuple(
        field("image", &PromptCapabilities::image),
        field("audio", &PromptCapabilities::audio),
        field("embeddedContext", &PromptCapabilities::embeddedContext));
}

constexpr auto jsonSchema(const McpCapabilities *)
{
    return std::make_tuple(
        field("http", &McpCapabilities::http), field("sse", &McpCapabilities::sse));
}

constexpr auto jsonSchema(const AgentCapabilities *)
{
    return std::make_tuple(
        field("loadSession", &AgentCapabilities::loadSession),
        field("promptCapabilities", &AgentCapabilities::promptCapabilities),
        field("mcpCapabilities", &AgentCapabilities::mcpCapabilities));
}

constexpr auto jsonExtras(const AgentCapabilities *)
{
    return &AgentCapabilities::extras;
}

constexpr auto jsonSchema(const AuthMethod *)
{
    return std::make_tuple(
        field("id", &AuthMethod::id),
        field("name", &AuthMethod::name),
        omitEmpty("description", &AuthMethod::description));
}

constexpr auto jsonSchema(const InitializeParams *)
{
    return std::make_tuple(
        field("protocolVersion", &InitializeParams::protocolVersion),
        field("clientCapabilities", &InitializeParams::clientCapabilities),
        field("clientInfo", &InitializeParams::clientInfo));
}

constexpr auto jsonExtras(const InitializeParams *)
{
    return &InitializeParams::extras;
}

constexpr auto jsonSchema(const InitializeResult *)
{
    return std::make_tuple(
        field("protocolVersion", &InitializeResult::protocolVersion),
        field("agentCapabilities", &InitializeResult::agentCapabilities),
        field("authMethods", &InitializeResult::authMethods),
        field("agentInfo", &InitializeResult::agentInfo));
}

constexpr auto jsonExtras(const InitializeResult *)
{
    return &InitializeResult::extras;
}

constexpr auto jsonSchema(const SessionMode *)
{
    return std::make_tuple(
        field("id", &SessionMode::id),
        field("name", &SessionMode::name),
        omitEmpty("description", &SessionMode::description));
}

constexpr auto jsonSchema(const SessionModeState *)
{
    return std::make_tuple(
        field("currentModeId", &SessionModeState::currentModeId),
        field("availableModes", &SessionModeState::availableModes));
}

constexpr auto jsonSchema(const SessionConfigSelectOption *)
{
    return std::make_tuple(
        field("value", &SessionConfigSelectOption::value),
        field("name", &SessionConfigSelectOption::name),
        omitEmpty("description", &SessionConfigSelectOption::description));
}

constexpr auto jsonSchema(const SessionConfigSelectGroup *)
{
    return std::make_tuple(
        field("group", &SessionConfigSelectGroup::group),
        field("name", &SessionConfigSelectGroup::name),
        field("options", &SessionConfigSelectGroup::options));
}

constexpr auto jsonSchema(const NewSessionParams *)
{
    return std::make_tuple(
        field("cwd", &NewSessionParams::cwd),
        field("mcpServers", &NewSessionParams::mcpServers),
        omitEmpty("additionalDirectories", &NewSessionParams::additionalDirectories));
}

constexpr auto jsonSchema(const NewSessionResult *)
{
    return std::make_tuple(
        field("sessionId", &NewSessionResult::sessionId),
        field("modes", &NewSessionResult::modes),
        omitEmpty("configOptions", &NewSessionResult::configOptions));
}

constexpr auto jsonExtras(const NewSessionResult *)
{
    return &NewSessionResult::extras;
}

constexpr auto jsonSchema(const LoadSessionParams *)
{
    return std::make_tuple(
        field("sessionId", &LoadSessionParams::sessionId),
        field("cwd", &LoadSessionParams::cwd),
        field("mcpServers", &LoadSessionParams::mcpServers),
        omitEmpty("additionalDirectories", &LoadSessionParams::additionalDirectories));
}

constexpr auto jsonSchema(const ContentBlock *)
{
    return std::make_tuple(
        field("type", &ContentBlock::type),
        field("text", &ContentBlock::text),
        field("data", &ContentBlock::data),
        field("mimeType", &ContentBlock::mimeType),
        field("uri", &ContentBlock::uri),
        field("name", &ContentBlock::name),
        field("description", &ContentBlock::description),
        field("title", &ContentBlock::title),
        field("size", &ContentBlock::size),
        field("resource", &ContentBlock::resource),
        field("annotations", &ContentBlock::annotations));
}

constexpr auto jsonSchema(const PromptParams *)
{
    return std::make_tuple(
        field("sessionId", &PromptParams::sessionId), field("prompt", &PromptParams::prompt));
}

constexpr auto jsonSchema(const PromptResult *)
{
    return std::make_tuple(
        field("stopReason", &PromptResult::stopReason),
        omitEmpty("usage", &PromptResult::usage));
}

constexpr auto jsonExtras(const PromptResult *)
{
    return &PromptResult::extras;
}

constexpr auto jsonSchema(const ToolCallLocation *)
{
    return std::make_tuple(
        field("path", &ToolCallLocation::path), field("line", &ToolCallLocation::line));
}

constexpr auto jsonSchema(const ToolCallContent *)
{
    return std::make_tuple(
        field("type", &ToolCallContent::type),
        field("content", &ToolCallContent::content),
        field("path", &ToolCallContent::path),
        field("oldText", &ToolCallContent::oldText),
        field("newText", &ToolCallContent::newText),
        field("terminalId", &ToolCallContent::terminalId));
}

constexpr auto jsonSchema(const ToolCall *)
{
    return std::make_tuple(
        field("toolCallId", &ToolCall::toolCallId),
        omitEmpty("title", &ToolCall::title),
        omitEmpty("kind", &ToolCall::kind),
        omitEmpty("status", &ToolCall::status),
        omitEmpty("content", &ToolCall::content),
        omitEmpty("locations", &ToolCall::locations),
        omitEmpty("rawInput", &ToolCall::rawInput),
        omitEmpty("rawOutput", &ToolCall::rawOutput));
}

constexpr auto jsonSchema(const PlanEntry *)
{
    return std::make_tuple(
        field("content", &PlanEntry::content),
        field("priority", &PlanEntry::priority),
        field("status", &PlanEntry::status));
}

constexpr auto jsonSchema(const Plan *)
{
    return std::make_tuple(field("entries", &Plan::entries));
}

constexpr auto jsonSchema(const SessionNotification *)
{
    return std::make_tuple(
        field("sessionId", &SessionNotification::sessionId),
        field("update", &SessionNotification::update));
}

constexpr auto jsonSchema(const PermissionOption *)
{
    return std::make_tuple(
        field("optionId", &PermissionOption::optionId),
        field("name", &PermissionOption::name),
        field("kind", &PermissionOption::kind));
}

constexpr auto jsonSchema(const RequestPermissionParams *)
{
    return std::make_tuple(
        field("sessionId", &RequestPermissionParams::sessionId),
        field("toolCall", &RequestPermissionParams::toolCall),
        field("options", &RequestPermissionParams::options));
}

constexpr auto jsonSchema(const ReadTextFileParams *)
{
    return std::make_tuple(
        field("sessionId", &ReadTextFileParams::sessionId),
        field("path", &ReadTextFileParams::path),
        field("line", &ReadTextFileParams::line),
        field("limit", &ReadTextFileParams::limit));
}

constexpr auto jsonSchema(const ReadTextFileResult *)
{
    return std::make_tuple(field("content", &ReadTextFileResult::content));
}

constexpr auto jsonSchema(const WriteTextFileParams *)
{
    return std::make_tuple(
        field("sessionId", &WriteTextFileParams::sessionId),
        field("path", &WriteTextFileParams::path),
        field("content", &WriteTextFileParams::content));
}

constexpr auto jsonSchema(const CreateTerminalParams *)
{
    return std::make_tuple(
        field("sessionId", &CreateTerminalParams::sessionId),
        field("command", &CreateTerminalParams::command),
        field("args", &CreateTerminalParams::args),
        field("env", &CreateTerminalParams::env),
        omitEmpty("cwd", &CreateTerminalParams::cwd),
        field("outputByteLimit", &CreateTerminalParams::outputByteLimit));
}

constexpr auto jsonSchema(const CreateTerminalResult *)
{
    return std::make_tuple(field("terminalId", &CreateTerminalResult::terminalId));
}

constexpr auto jsonSchema(const ExitStatus *)
{
    return std::make_tuple(
        nullWhenEmpty("exitCode", &ExitStatus::exitCode),
        nullWhenEmpty("signal", &ExitStatus::signal));
}

constexpr auto jsonSchema(const TerminalOutputParams *)
{
    return std::make_tuple(
        field("sessionId", &TerminalOutputParams::sessionId),
        field("terminalId", &TerminalOutputParams::terminalId));
}

constexpr auto jsonSchema(const TerminalOutputResult *)
{
    return std::make_tuple(
        field("output", &TerminalOutputResult::output),
        field("truncated", &TerminalOutputResult::truncated),
        field("exitStatus", &TerminalOutputResult::exitStatus));
}

constexpr auto jsonSchema(const TerminalRefParams *)
{
    return std::make_tuple(
        field("sessionId", &TerminalRefParams::sessionId),
        field("terminalId", &TerminalRefParams::terminalId));
}

constexpr auto jsonSchema(const WaitForTerminalExitResult *)
{
    return std::make_tuple(
        nullWhenEmpty("exitCode", &WaitForTerminalExitResult::exitCode),
        nullWhenEmpty("signal", &WaitForTerminalExitResult::signal));
}

} // namespace LLMQore::Acp
