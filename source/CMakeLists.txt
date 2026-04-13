target_sources(LLMCore PRIVATE
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ContentBlocks.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/BaseMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/BaseMessage.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/Log.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/Log.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/BaseClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/BaseClient.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMCore/HttpResponse.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpResponse.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/HttpTransportError.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpTransportError.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/HttpStream.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpStream.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/HttpClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpClient.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/HttpRequestParser.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpRequestParser.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/LineBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/LineBuffer.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/SSEParser.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/SSEParser.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/RequestMode.hpp

    ${PROJECT_SOURCE_DIR}/include/LLMCore/ClaudeClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/claude/ClaudeClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/claude/ClaudeMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/claude/ClaudeMessage.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMCore/OpenAIClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIMessage.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/OpenAIResponsesClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIResponsesClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIResponsesMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIResponsesMessage.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMCore/LlamaCppClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/llamacpp/LlamaCppClient.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMCore/OllamaClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/ollama/OllamaClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/ollama/OllamaMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/ollama/OllamaMessage.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMCore/GoogleAIClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/google/GoogleAIClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/google/GoogleMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/google/GoogleMessage.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMCore/BaseTool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/BaseTool.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ToolResult.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolResult.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ToolExceptions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolHandler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolHandler.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ToolsManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolsManager.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ToolSchemaFormat.hpp

    # --- MCP ---
    ${PROJECT_SOURCE_DIR}/include/LLMCore/Mcp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpTypes.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpExceptions.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpSession.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpClient.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpServer.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpRemoteTool.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpPipeTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpStdioTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpStdioServerTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpHttpTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/BaseResourceProvider.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/BasePromptProvider.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/BaseRootsProvider.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/BaseElicitationProvider.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/McpHttpServerTransport.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpTypes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpSession.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpRemoteTool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpPipeTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpLineFramer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpStdioClientTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpStdioServerTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpHttpTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/McpHttpServerTransport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/BaseResourceProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcp/BasePromptProvider.cpp
)
