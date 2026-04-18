find_package(Qt6 REQUIRED COMPONENTS Test Concurrent)

add_executable(LLMQoreIntegrationTests
    IntegrationTestTools.cpp
    tst_ClaudeIntegration.cpp
    tst_ClaudeMcpIntegration.cpp
    tst_OpenAIIntegration.cpp
    tst_OpenAIResponsesIntegration.cpp
    tst_OllamaIntegration.cpp
    tst_GoogleAIIntegration.cpp
    tst_LlamaCppIntegration.cpp
    tst_MistralIntegration.cpp
)

set_target_properties(LLMQoreIntegrationTests PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(LLMQoreIntegrationTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(LLMQoreIntegrationTests PRIVATE
    LLMQore::LLMQore
    Qt6::Test
    Qt6::Concurrent
    GTest::gtest_main
)

# Integration tests are NOT added to CTest by default — run manually
# or use: ctest -R Integration
add_test(NAME LLMQoreIntegrationTests COMMAND LLMQoreIntegrationTests)
set_tests_properties(LLMQoreIntegrationTests PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)
