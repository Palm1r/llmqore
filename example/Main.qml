// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts
import example.LLMCoreChat

import "qml"

ApplicationWindow {
    id: root

    readonly property var providers: [
        { name: "Claude",    url: "https://api.anthropic.com",                       needsKey: true  },
        { name: "OpenAI",    url: "https://api.openai.com",                          needsKey: true  },
        { name: "Ollama",    url: "http://localhost:11434",                          needsKey: false },
        { name: "Google AI", url: "https://generativelanguage.googleapis.com",       needsKey: true  },
        { name: "LlamaCpp",  url: "http://localhost:8080",                           needsKey: false },
    ]

    width: 800
    height: 600
    visible: true
    title: "LLMCore Chat"

    // -- Nord palette ---------------------------------------------------------

    palette {
        window:          "#2e3440"
        base:            "#3b4252"
        alternateBase:   "#434c5e"
        text:            "#eceff4"
        windowText:      "#eceff4"
        button:          "#4c566a"
        buttonText:      "#eceff4"
        highlight:       "#88c0d0"
        highlightedText: "#2e3440"
        placeholderText:  "#7b88a1"
        mid:             "#4c566a"
        dark:            "#2e3440"
        light:           "#e5e9f0"
    }

    color: palette.window

    ChatController { id: controller }

    ToolsDrawer {
        id: toolsDrawer
        toolNames: controller.toolNames
    }

    ColumnLayout {
        anchors {
            fill: parent
            leftMargin: 12
            rightMargin: 12
            topMargin: 6
        }
        spacing: 0

        // -- Chat messages ------------------------------------------------

        ListView {
            id: chatView

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            model: controller.messages

            delegate: Item {
                id: msgDelegate

                required property string role
                required property string text
                required property int index

                readonly property bool isToolInGroup: {
                    if (role !== "tool" || index === 0) return false
                    const prev = controller.messages.roleAt(index - 1)
                    return prev === "assistant" || prev === "tool"
                }

                width: ListView.view.width
                implicitHeight: delegateBubble.implicitHeight
                                + (isToolInGroup ? 1 : (index > 0 ? 6 : 0))

                ChatBubble {
                    id: delegateBubble

                    y: msgDelegate.isToolInGroup ? 1 : (msgDelegate.index > 0 ? 6 : 0)
                    width: parent.width
                    role: msgDelegate.role
                    messageText: msgDelegate.text
                    isToolInGroup: msgDelegate.isToolInGroup
                }
            }

            // Robust auto-scroll: keep at bottom during streaming
            onCountChanged: scrollToBottom()
            onContentHeightChanged: {
                if (atYEnd || controller.busy)
                    scrollToBottom()
            }

            function scrollToBottom() {
                Qt.callLater(() => positionViewAtEnd())
            }
        }

        // -- Separator ----------------------------------------------------

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 4
            implicitHeight: 1
            color: palette.alternateBase
        }

        // -- Provider bar -------------------------------------------------

        ProviderBar {
            id: providerBar

            Layout.fillWidth: true
            Layout.topMargin: 6
            providers: root.providers
            controller: controller

            onReconnectRequested: root.reconnect()
        }

        // -- Input bar ----------------------------------------------------

        ChatInput {
            Layout.fillWidth: true
            Layout.topMargin: 6
            busy: controller.busy
            toolCount: controller.toolNames.length

            onSendRequested: text => {
                if (!controller.modelList.length && providerBar.currentModel.length === 0)
                    return
                controller.send(text, providerBar.currentModel)
            }
            onStopRequested: controller.stopGeneration()
            onClearRequested: controller.clearChat()
            onToolsToggled: toolsDrawer.open()
        }

        // -- Status (shown when not busy) ---------------------------------

        Label {
            Layout.topMargin: 4
            Layout.leftMargin: 4
            Layout.bottomMargin: 4
            Layout.preferredHeight: 12
            visible: !controller.busy
            text: controller.status
            font.pixelSize: 11
            color: palette.placeholderText
        }

        // -- Typing indicator ---------------------------------------------

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.leftMargin: 4
            Layout.bottomMargin: 4
            Layout.preferredHeight: 12
            visible: controller.busy
            spacing: 4

            Repeater {
                model: 3
                delegate: Rectangle {
                    required property int index
                    width: 6; height: 6; radius: 3
                    color: "#88c0d0"

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        running: controller.busy
                        PauseAnimation { duration: index * 200 }
                        NumberAnimation { from: 0.3; to: 1.0; duration: 400 }
                        NumberAnimation { from: 1.0; to: 0.3; duration: 400 }
                    }
                }
            }

            Label {
                text: controller.status
                font.pixelSize: 11
                color: palette.placeholderText
            }
        }
    }

    function reconnect() {
        controller.setupProvider(
            providerBar.providerName(),
            providerBar.providerUrl(),
            providerBar.providerKey()
        )
    }

    Component.onCompleted: reconnect()
}
