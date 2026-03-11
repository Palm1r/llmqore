cmake_minimum_required(VERSION 3.21)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network Concurrent Quick)

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
