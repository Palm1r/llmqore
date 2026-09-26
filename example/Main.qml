// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts
import example.LLMQoreChat

import "qml"

ApplicationWindow {
    id: root

    readonly property var llmProviders: [
        {
            name: "Claude",
            url: "https://api.anthropic.com",
            needsKey: true
        },
        {
            name: "OpenAI",
            url: "https://api.openai.com/v1",
            needsKey: true
        },
        {
            name: "OpenAI Responses",
            url: "https://api.openai.com/v1",
            needsKey: true
        },
        {
            name: "DeepSeek",
            url: "https://api.deepseek.com/v1",
            needsKey: true
        },
        {
            name: "Mistral",
            url: "https://api.mistral.ai",
            needsKey: true
        },
        {
            name: "Ollama",
            url: "http://localhost:11434",
            needsKey: false
        },
        {
            name: "Google AI",
            url: "https://generativelanguage.googleapis.com/v1beta",
            needsKey: true
        },
        {
            name: "LlamaCpp",
            url: "http://localhost:8080",
            needsKey: false
        },
    ]

    readonly property var providers: llmProviders.concat(controller.acpAgentNames.map(n => ({
                name: n,
                url: "",
                needsKey: false
            })))

    width: 800
    height: 600
    visible: true
    title: "LLMQore Chat"

    palette {
        window: "#2e3440"
        base: "#3b4252"
        alternateBase: "#434c5e"
        text: "#eceff4"
        windowText: "#eceff4"
        button: "#4c566a"
        buttonText: "#eceff4"
        highlight: "#88c0d0"
        highlightedText: "#2e3440"
        placeholderText: "#7b88a1"
        mid: "#4c566a"
        dark: "#2e3440"
        light: "#e5e9f0"
    }

    color: palette.window

    ChatController {
        id: controller
    }

    ToolsDrawer {
        id: toolsDrawer
        toolNames: controller.session.toolNames
    }

    ColumnLayout {
        anchors {
            fill: parent
            leftMargin: 12
            rightMargin: 12
            topMargin: 6
        }
        spacing: 0

        ListView {
            id: chatView

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            model: controller.session.messages

            delegate: Item {
                id: msgDelegate

                required property string role
                required property string text
                required property int index

                readonly property bool isToolInGroup: {
                    if (role !== "tool" || index === 0)
                        return false;
                    const prev = controller.session.messages.roleAt(index - 1);
                    return prev === "assistant" || prev === "tool";
                }

                width: ListView.view.width
                implicitHeight: delegateBubble.implicitHeight + (isToolInGroup ? 1 : (index > 0 ? 6 : 0))

                ChatBubble {
                    id: delegateBubble

                    y: msgDelegate.isToolInGroup ? 1 : (msgDelegate.index > 0 ? 6 : 0)
                    width: parent.width
                    role: msgDelegate.role
                    messageText: msgDelegate.text
                    isToolInGroup: msgDelegate.isToolInGroup
                }
            }

            onCountChanged: scrollToBottom()
            onContentHeightChanged: {
                if (atYEnd || controller.session.busy)
                    scrollToBottom();
            }

            function scrollToBottom() {
                Qt.callLater(() => positionViewAtEnd());
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 4
            implicitHeight: 1
            color: palette.alternateBase
        }

        ProviderBar {
            id: providerBar

            Layout.fillWidth: true
            Layout.topMargin: 6
            providers: root.providers
            controller: controller

            onReconnectRequested: root.reconnect()
        }

        ChatInput {
            Layout.fillWidth: true
            Layout.topMargin: 6
            busy: controller.session.busy
            toolCount: controller.session.toolNames.length

            onSendRequested: text => {
                if (!controller.session.modelList.length && providerBar.currentModel.length === 0)
                    return;
                controller.session.send(text, providerBar.currentModel);
            }
            onStopRequested: controller.session.stop()
            onClearRequested: controller.session.clear()
            onToolsToggled: toolsDrawer.open()
        }

        Label {
            Layout.topMargin: 4
            Layout.leftMargin: 4
            Layout.bottomMargin: 4
            Layout.preferredHeight: 12
            visible: !controller.session.busy
            text: controller.session.status
            font.pixelSize: 11
            color: palette.placeholderText
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.leftMargin: 4
            Layout.bottomMargin: 4
            Layout.preferredHeight: 12
            visible: controller.session.busy
            spacing: 4

            Repeater {
                model: 3
                delegate: Rectangle {
                    required property int index
                    width: 6
                    height: 6
                    radius: 3
                    color: "#88c0d0"

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        running: controller.session.busy
                        PauseAnimation {
                            duration: index * 200
                        }
                        NumberAnimation {
                            from: 0.3
                            to: 1.0
                            duration: 400
                        }
                        NumberAnimation {
                            from: 1.0
                            to: 0.3
                            duration: 400
                        }
                    }
                }
            }

            Label {
                text: controller.session.status
                font.pixelSize: 11
                color: palette.placeholderText
            }
        }
    }

    function reconnect() {
        controller.setupProvider(providerBar.providerName(), providerBar.providerUrl(), providerBar.providerKey());
    }

    Component.onCompleted: reconnect()
}
