qt_add_executable(mcp-bridge
    main.cpp
    BridgeConfig.hpp
    BridgeConfig.cpp
    BridgeServer.hpp
    BridgeServer.cpp
)

target_link_libraries(mcp-bridge
    PRIVATE
    LLMCore::LLMCore
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

include(GNUInstallDirs)
install(TARGETS mcp-bridge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
