// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts
import example.LLMCoreChat

ApplicationWindow {
    id: root

    readonly property var providers: [
        { name: "Claude",    url: "https://api.anthropic.com",                       needsKey: true  },
        { name: "OpenAI",    url: "https://api.openai.com",                          needsKey: true  },
        { name: "Ollama",    url: "http://localhost:11434",                          needsKey: false },
        { name: "Google AI", url: "https://generativelanguage.googleapis.com",       needsKey: true  },
        { name: "LlamaCpp", url: "http://localhost:8080",                          needsKey: false },
    ]

    component ChatBubble : Rectangle {
        id: bubble

        required property string role
        required property string messageText
        required property bool isToolInGroup

        implicitHeight: contentCol.implicitHeight + 16
        radius: 6

        color: role === "user"  ? "#5e81ac"
             : role === "error" ? "#bf616a"
             : palette.base

        ColumnLayout {
            id: contentCol

            anchors {
                fill: parent
                margins: 8
            }
            spacing: 4

            Label {
                visible: !bubble.isToolInGroup
                text: bubble.role === "user"      ? qsTr("You")
                    : bubble.role === "assistant"  ? qsTr("Assistant")
                    : bubble.role === "error"      ? qsTr("Error")
                    : bubble.role
                font {
                    bold: true
                    pixelSize: 11
                }
                color: Qt.rgba(1, 1, 1, 0.5)
            }

            Rectangle {
                Layout.fillWidth: true

                visible: bubble.role === "tool"
                implicitHeight: toolCol.implicitHeight + 12
                radius: 4
                color: Qt.rgba(235, 203, 139, 0.12)    // nord13 tint
                border {
                    width: 1
                    color: Qt.rgba(235, 203, 139, 0.25)
                }

                ColumnLayout {
                    id: toolCol

                    anchors {
                        fill: parent
                        margins: 6
                    }
                    spacing: 2

                    Label {
                        text: {
                            const m = bubble.messageText.match(/^\[(.+?)\]:/)
                            return m ? "Tool: " + m[1] : "Tool"
                        }
                        font { bold: true; pixelSize: 10 }
                        color: "#ebcb8b"
                    }

                    Label {
                        Layout.fillWidth: true
                        text: {
                            const idx = bubble.messageText.indexOf("]: ")
                            return idx >= 0 ? bubble.messageText.substring(idx + 3)
                                            : bubble.messageText
                        }
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        font.pixelSize: 13
                        color: Qt.rgba(1, 1, 1, 0.8)
                    }
                }
            }

            Label {
                visible: bubble.role !== "tool"
                Layout.fillWidth: true
                text: bubble.messageText
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                font.pixelSize: 14
                color: palette.text
            }
        }
    }

    component ToolBadge : Label {

        required property string name

        text: name
        font.pixelSize: 11
        color: "#d8dee9"
        leftPadding: 6
        rightPadding: 6
        topPadding: 3
        bottomPadding: 3
        background: Rectangle {
            radius: 3
            color: palette.alternateBase
        }
    }

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

            onCountChanged: Qt.callLater(() => positionViewAtEnd())
        }

        Label {
            Layout.leftMargin: 12
            text: qsTr("Tools:")
            font { bold: true; pixelSize: 11 }
            color: palette.placeholderText
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            spacing: 4
            visible: controller.toolNames.length > 0

            Repeater {
                id: toolBadge

                model: controller.toolNames

                delegate: ToolBadge {
                    required property string modelData
                    name: modelData
                }
            }
        }

        Label {
            Layout.leftMargin: 12
            visible: controller.mcpServerNames.length > 0
            text: qsTr("MCP servers:")
            font { bold: true; pixelSize: 11 }
            color: palette.placeholderText
        }

        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 2
            Layout.bottomMargin: 4
            spacing: 4
            visible: controller.mcpServerNames.length > 0

            Repeater {
                model: controller.mcpServerNames
                delegate: ToolBadge {
                    required property string modelData
                    name: modelData
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: palette.alternateBase
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                ComboBox {
                    id: providerCombo

                    Layout.preferredWidth: 120
                    model: root.providers.map(p => p.name)
                    onCurrentIndexChanged: {
                        urlField.text = root.providers[currentIndex].url
                        apiKeyField.text = ""
                        root.reconnect()
                    }
                }

                TextField {
                    id: urlField

                    Layout.fillWidth: true
                    text: root.providers[0].url
                    placeholderText: qsTr("API URL")
                    onEditingFinished: root.reconnect()
                }

                TextField {
                    id: apiKeyField

                    Layout.fillWidth: true
                    visible: root.providers[providerCombo.currentIndex].needsKey
                    echoMode: TextInput.Password
                    placeholderText: qsTr("API Key")
                    onEditingFinished: root.reconnect()
                }

                ComboBox {
                    id: modelCombo

                    Layout.preferredWidth: 200
                    model: controller.modelList
                    editable: true
                    enabled: !controller.loadingModels
                    displayText: controller.loadingModels ? qsTr("Loading…") : currentText
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56

                    TextArea {
                        id: inputField

                        placeholderText: qsTr("Type a message… (Enter to send, Shift+Enter for newline)")
                        wrapMode: Text.WordWrap
                        enabled: !controller.busy

                        Keys.onReturnPressed: event => {
                            if (event.modifiers & Qt.ShiftModifier) {
                                event.accepted = false
                            } else {
                                root.sendAction()
                                event.accepted = true
                            }
                        }
                    }
                }

                Button {
                    text: controller.busy ? qsTr("Stop") : qsTr("Send")
                    enabled: controller.busy || modelCombo.currentText.length > 0
                    Layout.preferredHeight: 56
                    Layout.preferredWidth: 72
                    onClicked: controller.busy ? controller.stopGeneration() : root.sendAction()
                }

                Button {
                    text: qsTr("Clear")
                    enabled: !controller.busy
                    Layout.preferredHeight: 56
                    Layout.preferredWidth: 72
                    onClicked: controller.clearChat()
                }
            }

            Label {
                text: controller.status
                font.pixelSize: 11
                color: palette.placeholderText
            }
        }
    }

    function sendAction() {
        const text = inputField.text.trim()
        if (text.length === 0) return
        if (!controller.modelList.length && modelCombo.currentText.length === 0) return

        controller.send(inputField.text, modelCombo.currentText)
        inputField.text = ""
    }

    function reconnect() {
        controller.setupProvider(
            providerCombo.currentText,
            urlField.text.trim(),
            apiKeyField.text.trim()
        )
    }

    Component.onCompleted: reconnect()
}
