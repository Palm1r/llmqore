// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT
//
// Example: expose a couple of host-inspection BaseTools as an MCP server over
// stdio. Build this binary, then point Claude Desktop (or any other MCP
// client) at it via its MCP config file. The chat example ships this binary
// as the default stdio server in mcp-servers.json, so once both are built,
// launching example-chat.exe auto-spawns it and its tools appear in the UI.
//
//   {
//     "mcpServers": {
//       "llmcore-host": {
//         "command": "C:/path/to/example-mcp-server.exe"
//       }
//     }
//   }
//
// Tools exposed: get_ipv4, get_env, read_image. Kept intentionally different
// from the chat example's built-in DateTimeTool / CalculatorTool /
// SystemInfoTool so that, side-by-side, the user sees distinct tools rather
// than the MCP set shadowing the locals. read_image is the flagship rich-
// content demo: it returns a real image as a base64 content block and
// demonstrates rich ToolResult flowing all the way through McpServer →
// McpRemoteTool → ClaudeClient continuation to the model.

#include <QCoreApplication>

#include <LLMCore/Mcp>

#include "../ExampleTools.hpp"

using namespace LLMCore::Mcp;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    auto *transport = new McpStdioServerTransport(&app);

    McpServerConfig cfg;
    cfg.serverInfo = {"llmcore-host", "0.1.0"};
    cfg.instructions
        = "Example MCP server exposing host-inspection tools (IPv4, environment "
          "variables, image file reader) via llmcore.";

    auto *server = new McpServer(transport, cfg, &app);
    server->addTool(new Example::IPv4Tool(server));
    server->addTool(new Example::EnvTool(server));
    server->addTool(new Example::ImageReadTool(server));

    server->start();
    return app.exec();
}
