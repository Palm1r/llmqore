if(NOT DEFINED QT_VERSION_MAJOR)
    find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Test Concurrent)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Concurrent)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(LLMQoreUnitTests
    main.cpp
    tst_ContentBlocks.cpp
    tst_LineBuffer.cpp
    tst_SSEParser.cpp
    tst_ToolsManager.cpp
    tst_ToolHandler.cpp
    tst_ToolLoopRunner.cpp
    tst_ClaudeMessage.cpp
    tst_OpenAIMessage.cpp
    tst_OpenAIResponsesMessage.cpp
    tst_GoogleMessage.cpp
    tst_OllamaMessage.cpp
    tst_ClientConstructors.cpp
    tst_McpLineFramer.cpp
    tst_McpTypes.cpp
    tst_McpLoopback.cpp
    tst_AcpTypes.cpp
    tst_AcpLoopback.cpp
    tst_AcpProviders.cpp
    tst_AcpAgentConfig.cpp
    tst_AcpAgentRegistry.cpp
    tst_McpHttpServer.cpp
    tst_ToolResult.cpp
    tst_HttpClient.cpp
    tst_HttpRequestParser.cpp
    tst_TokenUsage.cpp
)

set_target_properties(LLMQoreUnitTests PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(QT_VERSION_MAJOR EQUAL 5)
    set_property(TARGET LLMQoreUnitTests PROPERTY CXX_STANDARD 17)
endif()

target_include_directories(LLMQoreUnitTests PRIVATE
    ${PROJECT_SOURCE_DIR}/source
)

target_link_libraries(LLMQoreUnitTests PRIVATE
    LLMQore::LLMQore
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Concurrent
    GTest::gtest
)

add_test(NAME LLMQoreUnitTests COMMAND LLMQoreUnitTests)

option(LLMQORE_BUILD_INTEGRATION_TESTS "Build LLMQore integration tests (require API keys)" OFF)
if(LLMQORE_BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
