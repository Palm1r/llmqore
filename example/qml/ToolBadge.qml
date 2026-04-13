// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls.Fusion

Label {
    id: badge

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
