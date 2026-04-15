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

if(APPLE)
    set_target_properties(mcp-bridge PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "mcp-bridge"
        MACOSX_BUNDLE_GUI_IDENTIFIER "ai.llmqore.mcp-bridge"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
elseif(UNIX)
    set_target_properties(mcp-bridge PROPERTIES
        INSTALL_RPATH "$ORIGIN:$ORIGIN/lib"
    )
endif()

if(APPLE)
    install(TARGETS mcp-bridge
        BUNDLE DESTINATION .
        COMPONENT mcp-bridge
    )
else()
    install(TARGETS mcp-bridge
        RUNTIME DESTINATION .
        COMPONENT mcp-bridge
    )
endif()

install(FILES mcp-bridge.json
    DESTINATION .
    COMPONENT mcp-bridge
)

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
    set(_macdeployqt "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}/macdeployqt")
    install(CODE "
        message(STATUS \"Running macdeployqt on \${CMAKE_INSTALL_PREFIX}/mcp-bridge.app\")
        execute_process(
            COMMAND \"${_macdeployqt}\"
                \"\${CMAKE_INSTALL_PREFIX}/mcp-bridge.app\"
                -always-overwrite
                -codesign=-
            RESULT_VARIABLE _deploy_res
        )
        if(NOT _deploy_res EQUAL 0)
            message(FATAL_ERROR \"macdeployqt failed: \${_deploy_res}\")
        endif()
        message(STATUS \"Sealing bundle signature\")
        execute_process(
            COMMAND codesign --force --sign -
                \"\${CMAKE_INSTALL_PREFIX}/mcp-bridge.app\"
            RESULT_VARIABLE _sign_res
        )
        if(NOT _sign_res EQUAL 0)
            message(FATAL_ERROR \"codesign failed: \${_sign_res}\")
        endif()
    " COMPONENT mcp-bridge)
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
