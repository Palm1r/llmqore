target_sources(LLMQore PRIVATE
    ${PROJECT_SOURCE_DIR}/include/LLMQore/ContentBlocks.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/BaseMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/BaseMessage.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/Log.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/Log.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/BaseClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/BaseClient.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMQore/HttpResponse.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpResponse.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/HttpTransportError.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpTransportError.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/HttpStream.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpStream.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/HttpClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpClient.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/HttpRequestParser.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpRequestParser.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/LineBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/LineBuffer.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/SSEParser.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/SSEParser.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/RequestMode.hpp

    ${PROJECT_SOURCE_DIR}/include/LLMQore/ClaudeClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/claude/ClaudeClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/claude/ClaudeMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/claude/ClaudeMessage.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMQore/OpenAIClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIMessage.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/OpenAIResponsesClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIResponsesClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIResponsesMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/openai/OpenAIResponsesMessage.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMQore/LlamaCppClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/llamacpp/LlamaCppClient.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMQore/MistralClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/mistral/MistralClient.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMQore/OllamaClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/ollama/OllamaClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/ollama/OllamaMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/ollama/OllamaMessage.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMQore/GoogleAIClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/google/GoogleAIClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/google/GoogleMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/clients/google/GoogleMessage.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMQore/BaseTool.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/BaseTool.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/ToolResult.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolResult.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/ToolExceptions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolHandler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolHandler.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/ToolRegistry.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolRegistry.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/ToolsManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolsManager.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/ToolSchemaFormat.hpp

    # --- MCP ---
    ${PROJECT_SOURCE_DIR}/include/LLMQore/Mcp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpTypes.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpExceptions.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpSession.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpClient.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpServer.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpRemoteTool.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpPipeTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpStdioTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpStdioServerTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpHttpTransport.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/BaseResourceProvider.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/BasePromptProvider.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/BaseRootsProvider.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/BaseElicitationProvider.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMQore/McpHttpServerTransport.hpp
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
