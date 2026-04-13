// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

RowLayout {
    id: inputBar

    required property bool busy
    required property int toolCount

    signal sendRequested(string text)
    signal stopRequested
    signal clearRequested
    signal toolsToggled

    spacing: 6

    Button {
        text: "\u2699 %1".arg(inputBar.toolCount)
        Layout.preferredHeight: 56
        Layout.preferredWidth: 56
        onClicked: inputBar.toolsToggled()

        ToolTip.visible: hovered
        ToolTip.text: qsTr("Show tools panel")
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 56

        TextArea {
            id: inputField

            placeholderText: qsTr("Type a message\u2026 (Enter to send, Shift+Enter for newline)")
            wrapMode: Text.WordWrap
            enabled: !inputBar.busy

            Keys.onReturnPressed: event => {
                if (event.modifiers & Qt.ShiftModifier) {
                    event.accepted = false
                } else {
                    doSend()
                    event.accepted = true
                }
            }
        }
    }

    Button {
        text: inputBar.busy ? qsTr("Stop") : qsTr("Send")
        enabled: inputBar.busy || inputField.text.trim().length > 0
        Layout.preferredHeight: 56
        Layout.preferredWidth: 72
        onClicked: inputBar.busy ? inputBar.stopRequested() : doSend()
    }

    Button {
        text: qsTr("Clear")
        enabled: !inputBar.busy
        Layout.preferredHeight: 56
        Layout.preferredWidth: 72
        onClicked: inputBar.clearRequested()
    }

    function doSend() {
        const text = inputField.text.trim()
        if (text.length === 0) return
        inputBar.sendRequested(inputField.text)
        inputField.text = ""
    }
}
