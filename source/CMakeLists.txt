target_sources(LLMCore PRIVATE
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ContentBlocks.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/BaseMessage.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/BaseMessage.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/Log.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/Log.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/BaseClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/BaseClient.cpp

    ${PROJECT_SOURCE_DIR}/include/LLMCore/HttpClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/HttpClient.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/SSEUtils.hpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/SSEBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/SSEBuffer.cpp
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
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ToolExceptions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolHandler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolHandler.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ToolsManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/ToolsManager.cpp
    ${PROJECT_SOURCE_DIR}/include/LLMCore/ToolSchemaFormat.hpp
)
