cmake_minimum_required(VERSION 3.21)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent Quick)

# Locate windeployqt next to qmake so we can drop all needed Qt runtime
# (platform plugin, QML modules, DLLs) next to the example binaries on
# Windows. Makes them standalone and launchable from Qt Creator, Explorer,
# or any shell regardless of PATH / plugin env.
if(WIN32)
    get_target_property(_qt_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_exe}" DIRECTORY)
    set(_windeployqt "${_qt_bin_dir}/windeployqt.exe")
endif()

qt_add_executable(example-chat
    main.cpp
    MessageModel.hpp
    ChatController.hpp
    ChatController.cpp
    ExampleTools.hpp
)

qt_add_qml_module(example-chat
    URI example.LLMCoreChat
    VERSION 1.0
    QML_FILES
    Main.qml
)

target_link_libraries(example-chat
    PRIVATE
    LLMCore::LLMCore
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    Qt6::Quick
)

# Ship the MCP server config template next to the chat executable so it is
# picked up automatically on startup.
add_custom_command(
    TARGET example-chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/mcp-servers.json
        $<TARGET_FILE_DIR:example-chat>/mcp-servers.json
    VERBATIM
)

# On Windows, run windeployqt so the example-chat binary ships with its
# platform plugin, QML modules, and Qt DLLs. Without this the exe cannot
# start when launched outside a Qt-aware shell environment.
if(WIN32 AND EXISTS "${_windeployqt}")
    add_custom_command(
        TARGET example-chat POST_BUILD
        COMMAND "${_windeployqt}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
            "$<TARGET_FILE:example-chat>"
        COMMENT "Deploying Qt runtime next to example-chat.exe"
        VERBATIM
    )
endif()

# MCP server demo: publish local BaseTools over stdio, launchable from Claude Desktop.
qt_add_executable(example-mcp-server
    mcp-server-demo/main.cpp
    ExampleTools.hpp
)
target_link_libraries(example-mcp-server
    PRIVATE
    LLMCore::LLMCore
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
)
set_target_properties(example-mcp-server PROPERTIES AUTOMOC ON)

if(WIN32 AND EXISTS "${_windeployqt}")
    add_custom_command(
        TARGET example-mcp-server POST_BUILD
        COMMAND "${_windeployqt}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            "$<TARGET_FILE:example-mcp-server>"
        COMMENT "Deploying Qt runtime next to example-mcp-server.exe"
        VERBATIM
    )
endif()

# Smoke-test utility for McpHttpTransport (both legacy and streamable paths).
qt_add_executable(example-mcp-http-probe
    mcp-http-probe/main.cpp
)
target_link_libraries(example-mcp-http-probe
    PRIVATE
    LLMCore::LLMCore
    Qt6::Core
    Qt6::Network
)
set_target_properties(example-mcp-http-probe PROPERTIES AUTOMOC ON)

if(WIN32 AND EXISTS "${_windeployqt}")
    add_custom_command(
        TARGET example-mcp-http-probe POST_BUILD
        COMMAND "${_windeployqt}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            "$<TARGET_FILE:example-mcp-http-probe>"
        COMMENT "Deploying Qt runtime next to example-mcp-http-probe.exe"
        VERBATIM
    )
endif()
