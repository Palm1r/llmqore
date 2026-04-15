qt_add_executable(mcp-bridge
    main.cpp
    BridgeConfig.hpp
    BridgeConfig.cpp
    BridgeServer.hpp
    BridgeServer.cpp
)

target_link_libraries(mcp-bridge
    PRIVATE
    LLMQore::LLMQore
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
)

set_target_properties(mcp-bridge PROPERTIES AUTOMOC ON)

if(UNIX AND NOT APPLE)
    set_target_properties(mcp-bridge PROPERTIES
        INSTALL_RPATH "$ORIGIN:$ORIGIN/lib"
    )
elseif(APPLE)
    set_target_properties(mcp-bridge PROPERTIES
        INSTALL_RPATH "@executable_path;@executable_path/../Frameworks"
    )
endif()

# Flat layout: binary + config at the archive root, Qt runtime next to it.
install(TARGETS mcp-bridge
    RUNTIME DESTINATION .
    COMPONENT mcp-bridge
)

install(FILES mcp-bridge.json
    DESTINATION .
    COMPONENT mcp-bridge
)

# --- Bundle Qt runtime next to the binary ---
if(WIN32)
    set(_windeployqt "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}/windeployqt.exe")
    install(CODE "
        execute_process(
            COMMAND \"${_windeployqt}\"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-quick-import
                \"\${CMAKE_INSTALL_PREFIX}/mcp-bridge.exe\"
            COMMAND_ERROR_IS_FATAL ANY
        )
    " COMPONENT mcp-bridge)
elseif(APPLE)
    # Copy minimal frameworks; CMake rewrites the binary's rpath to
    # INSTALL_RPATH automatically on install.
    foreach(_fw QtCore QtNetwork QtConcurrent)
        install(DIRECTORY
            "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}/${_fw}.framework"
            DESTINATION .
            COMPONENT mcp-bridge
            USE_SOURCE_PERMISSIONS
            PATTERN "Headers" EXCLUDE
            PATTERN "*.prl" EXCLUDE
            PATTERN "*_debug*" EXCLUDE
        )
    endforeach()
else()
    foreach(_lib Qt6Core Qt6Network Qt6Concurrent)
        file(GLOB _files
            "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}/lib${_lib}.so*"
        )
        install(FILES ${_files}
            DESTINATION lib
            COMPONENT mcp-bridge
        )
    endforeach()
    # ICU is a Qt runtime dep on Linux distributions we build against.
    foreach(_icu icui18n icuuc icudata)
        file(GLOB _icu_files
            "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}/lib${_icu}.so*"
        )
        if(_icu_files)
            install(FILES ${_icu_files}
                DESTINATION lib
                COMPONENT mcp-bridge
            )
        endif()
    endforeach()
endif()
