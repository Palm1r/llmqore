cmake_minimum_required(VERSION 3.21)

project(LLMQore
    VERSION 0.4.0
    LANGUAGES CXX
    DESCRIPTION "Qt/C++ library for LLM provider integration"
)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/LLMQore/Version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/LLMQore/Version.hpp
    @ONLY
)

add_library(LLMQore)

add_library(LLMQore::LLMQore ALIAS LLMQore)

set_target_properties(LLMQore PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_subdirectory(source)

target_include_directories(LLMQore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
)

target_link_libraries(LLMQore
    PUBLIC
        Qt6::Core
        Qt6::Network
    PRIVATE
        Qt6::Concurrent
)

target_compile_definitions(LLMQore PRIVATE LLMQORE_LIBRARY)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(LLMQore PUBLIC LLMQORE_STATIC_LIB)
endif()

target_compile_options(LLMQore PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# --- Installation ---
option(LLMQORE_INSTALL "Generate install targets" ${PROJECT_IS_TOP_LEVEL})

if(LLMQORE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS LLMQore
        EXPORT LLMQoreTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY include/LLMQore
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/LLMQore/Version.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/LLMQore
    )

    install(EXPORT LLMQoreTargets
        FILE LLMQoreTargets.cmake
        NAMESPACE LLMQore::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LLMQore
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LLMQoreConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/LLMQoreConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LLMQore
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/LLMQoreConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/LLMQoreConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/LLMQoreConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LLMQore
    )
endif()

option(LLMQORE_BUILD_TESTS "Build LLMQore tests" ${PROJECT_IS_TOP_LEVEL})
if(LLMQORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(LLMQORE_BUILD_EXAMPLES "Build LLMQore examples" ${PROJECT_IS_TOP_LEVEL})
if(LLMQORE_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

option(LLMQORE_BUILD_MCP_BRIDGE "Build MCP Bridge" ${PROJECT_IS_TOP_LEVEL})
if(LLMQORE_BUILD_MCP_BRIDGE)
    add_subdirectory(mcp-bridge)
endif()
