find_package(Qt6 REQUIRED COMPONENTS Test Concurrent)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(LLMCoreUnitTests
    main.cpp
    tst_ContentBlocks.cpp
    tst_LineBuffer.cpp
    tst_SSEParser.cpp
    tst_ToolsManager.cpp
    tst_ToolHandler.cpp
    tst_ClaudeMessage.cpp
    tst_OpenAIMessage.cpp
    tst_OpenAIResponsesMessage.cpp
    tst_GoogleMessage.cpp
    tst_OllamaMessage.cpp
    tst_ClientConstructors.cpp
    tst_McpLineFramer.cpp
    tst_McpTypes.cpp
    tst_McpLoopback.cpp
    tst_McpHttpServer.cpp
    tst_ToolResult.cpp
    tst_HttpClient.cpp
    tst_HttpRequestParser.cpp
)

set_target_properties(LLMCoreUnitTests PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(LLMCoreUnitTests PRIVATE
    ${PROJECT_SOURCE_DIR}/source
)

target_link_libraries(LLMCoreUnitTests PRIVATE
    LLMCore::LLMCore
    Qt6::Test
    Qt6::Concurrent
    GTest::gtest
)

add_test(NAME LLMCoreUnitTests COMMAND LLMCoreUnitTests)

option(LLMCORE_BUILD_INTEGRATION_TESTS "Build LLMCore integration tests (require API keys)" OFF)
if(LLMCORE_BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
