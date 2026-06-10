if(NOT DEFINED QT_VERSION_MAJOR)
    find_package(QT NAMES Qt5 Qt6 REQUIRED COMPONENTS Core Network Concurrent)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Concurrent)

add_executable(mcp-bridge
    main.cpp
    BridgeConfig.hpp
    BridgeConfig.cpp
    BridgeServer.hpp
    BridgeServer.cpp
)

set_target_properties(mcp-bridge PROPERTIES
    AUTOMOC ON
    CXX_STANDARD ${LLMQORE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_link_libraries(mcp-bridge
    PRIVATE
    LLMQore::LLMQore
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Concurrent
)

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
    get_target_property(_qt_qmake_exe Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_exe}" DIRECTORY)
    set(_windeployqt "${_qt_bin_dir}/windeployqt.exe")
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
    get_target_property(_qt_qmake_exe Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_exe}" DIRECTORY)
    set(_macdeployqt "${_qt_bin_dir}/macdeployqt")
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
    get_target_property(_qt_core_lib Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION)
    get_filename_component(_qt_lib_dir "${_qt_core_lib}" DIRECTORY)

    foreach(_lib Core Network Concurrent)
        file(GLOB _files
            "${_qt_lib_dir}/libQt${QT_VERSION_MAJOR}${_lib}.so*"
        )
        install(FILES ${_files}
            DESTINATION lib
            COMPONENT mcp-bridge
        )
    endforeach()
    if(QT_VERSION_MAJOR GREATER_EQUAL 6)
        foreach(_icu icui18n icuuc icudata)
            file(GLOB _icu_files
                "${_qt_lib_dir}/lib${_icu}.so*"
            )
            if(_icu_files)
                install(FILES ${_icu_files}
                    DESTINATION lib
                    COMPONENT mcp-bridge
                )
            endif()
        endforeach()
    endif()
endif()
