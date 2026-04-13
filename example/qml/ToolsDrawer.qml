// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

Drawer {
    id: drawer

    required property var toolNames

    edge: Qt.RightEdge
    width: Math.min(parent.width * 0.35, 320)
    height: parent.height

    background: Rectangle {
        color: "#2e3440"
        border { width: 1; color: "#3b4252" }
    }

    ColumnLayout {
        anchors {
            fill: parent
            margins: 12
        }
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("Tools (%1)").arg(drawer.toolNames.length)
                font { bold: true; pixelSize: 14 }
                color: "#eceff4"
                Layout.fillWidth: true
            }

            ToolButton {
                text: "\u2715"
                font.pixelSize: 14
                onClicked: drawer.close()
                palette.buttonText: "#eceff4"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: "#434c5e"
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4

            model: drawer.toolNames

            delegate: Rectangle {
                required property string modelData
                required property int index

                width: ListView.view.width
                implicitHeight: toolInfoCol.implicitHeight + 12
                radius: 4
                color: "#3b4252"

                ColumnLayout {
                    id: toolInfoCol

                    anchors {
                        fill: parent
                        margins: 6
                    }
                    spacing: 2

                    Label {
                        text: {
                            const sep = modelData.indexOf(" - ")
                            return sep >= 0 ? modelData.substring(0, sep) : modelData
                        }
                        font { bold: true; pixelSize: 12 }
                        color: "#88c0d0"
                    }

                    Label {
                        Layout.fillWidth: true
                        text: {
                            const sep = modelData.indexOf(" - ")
                            return sep >= 0 ? modelData.substring(sep + 3) : ""
                        }
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: "#d8dee9"
                        visible: text.length > 0
                    }
                }
            }
        }
    }
}
