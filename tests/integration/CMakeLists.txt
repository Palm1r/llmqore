if(NOT DEFINED QT_VERSION_MAJOR)
    find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Test Concurrent)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Concurrent)

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

if(QT_VERSION_MAJOR EQUAL 5)
    set_property(TARGET LLMQoreIntegrationTests PROPERTY CXX_STANDARD 17)
endif()

target_include_directories(LLMQoreIntegrationTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(LLMQoreIntegrationTests PRIVATE
    LLMQore::LLMQore
    Qt${QT_VERSION_MAJOR}::Test
    Qt${QT_VERSION_MAJOR}::Concurrent
    GTest::gtest_main
)

# Integration tests are NOT added to CTest by default — run manually
# or use: ctest -R Integration
add_test(NAME LLMQoreIntegrationTests COMMAND LLMQoreIntegrationTests)
set_tests_properties(LLMQoreIntegrationTests PROPERTIES
    LABELS "integration"
    TIMEOUT 300
)
