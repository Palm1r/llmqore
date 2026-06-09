cmake_minimum_required(VERSION 3.21)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif()

if(NOT DEFINED QT_VERSION_MAJOR)
    find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core Network Concurrent Quick)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Concurrent Quick)

# Locate windeployqt next to qmake so we can drop all needed Qt runtime
# (platform plugin, QML modules, DLLs) next to the example binaries on
# Windows. Makes them standalone and launchable from Qt Creator, Explorer,
# or any shell regardless of PATH / plugin env.
if(WIN32)
    get_target_property(_qt_qmake_exe Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_exe}" DIRECTORY)
    set(_windeployqt "${_qt_bin_dir}/windeployqt.exe")
endif()

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(example-chat
        main.cpp
        MessageModel.hpp
        ChatController.hpp
        ChatController.cpp
        ExampleTools.hpp
    )

    qt_add_qml_module(example-chat
        URI example.LLMQoreChat
        VERSION 1.0
        QML_FILES
            Main.qml
            qml/ChatBubble.qml
            qml/ChatInput.qml
            qml/ProviderBar.qml
            qml/ToolBadge.qml
            qml/ToolsDrawer.qml
    )

    target_link_libraries(example-chat
        PRIVATE
        LLMQore::LLMQore
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Concurrent
        Qt${QT_VERSION_MAJOR}::Quick
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
endif()

# MCP server demo: publish local BaseTools over stdio, launchable from Claude Desktop.
add_executable(example-mcp-server
    mcp-server-demo/main.cpp
    ExampleTools.hpp
)
set_target_properties(example-mcp-server PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_link_libraries(example-mcp-server
    PRIVATE
    LLMQore::LLMQore
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Concurrent
)

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
add_executable(example-mcp-http-probe
    mcp-http-probe/main.cpp
)
set_target_properties(example-mcp-http-probe PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_link_libraries(example-mcp-http-probe
    PRIVATE
    LLMQore::LLMQore
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)

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
