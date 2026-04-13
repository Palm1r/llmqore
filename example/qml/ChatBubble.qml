// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

Rectangle {
    id: bubble

    required property string role
    required property string messageText
    required property bool isToolInGroup

    readonly property bool isTool: role === "tool"
    readonly property bool isCollapsible: isTool && messageText.length > 200

    property bool expanded: !isCollapsible

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

        // -- Tool result (collapsible) --
        Rectangle {
            Layout.fillWidth: true
            visible: bubble.isTool
            implicitHeight: toolCol.implicitHeight + 12
            radius: 4
            color: Qt.rgba(235, 203, 139, 0.12)
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

                RowLayout {
                    spacing: 4

                    Label {
                        text: {
                            const m = bubble.messageText.match(/^\[(.+?)\]:/)
                            return m ? "Tool: " + m[1] : "Tool"
                        }
                        font { bold: true; pixelSize: 10 }
                        color: "#ebcb8b"
                    }

                    Label {
                        visible: bubble.isCollapsible
                        text: bubble.expanded ? "\u25B4 collapse" : "\u25BE expand"
                        font.pixelSize: 10
                        color: "#88c0d0"

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: bubble.expanded = !bubble.expanded
                        }
                    }
                }

                TextEdit {
                    Layout.fillWidth: true
                    visible: bubble.expanded
                    text: {
                        const idx = bubble.messageText.indexOf("]: ")
                        return idx >= 0 ? bubble.messageText.substring(idx + 3)
                                        : bubble.messageText
                    }
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    font.pixelSize: 13
                    color: Qt.rgba(1, 1, 1, 0.8)
                    readOnly: true
                    selectByMouse: true
                    selectedTextColor: "#2e3440"
                    selectionColor: "#88c0d0"
                }
            }
        }

        // -- Regular message (selectable, markdown) --
        TextEdit {
            visible: !bubble.isTool
            Layout.fillWidth: true
            text: bubble.messageText
            wrapMode: Text.WordWrap
            textFormat: bubble.role === "assistant" ? Text.MarkdownText : Text.PlainText
            font.pixelSize: 14
            color: palette.text
            readOnly: true
            selectByMouse: true
            selectedTextColor: "#2e3440"
            selectionColor: "#88c0d0"
        }
    }

    // -- Context menu --
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: mouse => contextMenu.popup()
        z: -1
    }

    Menu {
        id: contextMenu

        MenuItem {
            text: qsTr("Copy")
            onTriggered: {
                let t = bubble.messageText
                if (bubble.isTool) {
                    const idx = t.indexOf("]: ")
                    if (idx >= 0) t = t.substring(idx + 3)
                }
                clipHelper.text = t
                clipHelper.selectAll()
                clipHelper.copy()
            }
        }

        MenuItem {
            text: qsTr("Copy as Markdown")
            visible: bubble.role === "assistant"
            onTriggered: {
                clipHelper.text = bubble.messageText
                clipHelper.selectAll()
                clipHelper.copy()
            }
        }
    }

    TextEdit {
        id: clipHelper
        visible: false
    }
}
