cmake_minimum_required(VERSION 3.21)

project(LLMCore
    VERSION 0.2.1
    LANGUAGES CXX
    DESCRIPTION "Qt/C++ library for LLM provider integration"
)

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/LLMCore/Version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/LLMCore/Version.hpp
    @ONLY
)

add_library(LLMCore)

add_library(LLMCore::LLMCore ALIAS LLMCore)

set_target_properties(LLMCore PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_subdirectory(source)

target_include_directories(LLMCore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
)

target_link_libraries(LLMCore
    PUBLIC
        Qt6::Core
        Qt6::Network
    PRIVATE
        Qt6::Concurrent
)

target_compile_definitions(LLMCore PRIVATE LLMCORE_LIBRARY)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(LLMCore PUBLIC LLMCORE_STATIC_LIB)
endif()

target_compile_options(LLMCore PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# --- Installation ---
option(LLMCORE_INSTALL "Generate install targets" ${PROJECT_IS_TOP_LEVEL})

if(LLMCORE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS LLMCore
        EXPORT LLMCoreTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY include/LLMCore
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/LLMCore/Version.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/LLMCore
    )

    install(EXPORT LLMCoreTargets
        FILE LLMCoreTargets.cmake
        NAMESPACE LLMCore::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LLMCore
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/LLMCoreConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/LLMCoreConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LLMCore
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/LLMCoreConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/LLMCoreConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/LLMCoreConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/LLMCore
    )
endif()

option(LLMCORE_BUILD_TESTS "Build LLMCore tests" ${PROJECT_IS_TOP_LEVEL})
if(LLMCORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(LLMCORE_BUILD_EXAMPLES "Build LLMCore examples" ${PROJECT_IS_TOP_LEVEL})
if(LLMCORE_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

option(LLMCORE_BUILD_MCP_BRIDGE "Build MCP Bridge" ${PROJECT_IS_TOP_LEVEL})
if(LLMCORE_BUILD_MCP_BRIDGE)
    add_subdirectory(mcp-bridge)
endif()
