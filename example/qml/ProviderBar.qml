// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls.Fusion
import QtQuick.Layouts

RowLayout {
    id: bar

    required property var providers
    required property var controller

    readonly property string currentModel: modelCombo.currentText

    signal reconnectRequested

    spacing: 6

    ComboBox {
        id: providerCombo

        Layout.preferredWidth: 120
        model: bar.providers.map(p => p.name)
        onCurrentIndexChanged: {
            urlField.text = bar.providers[currentIndex].url
            apiKeyField.text = bar.controller.envApiKey(currentText)
            Qt.callLater(bar.reconnectRequested)
        }
    }

    TextField {
        id: urlField

        Layout.fillWidth: true
        text: bar.providers[0].url
        placeholderText: qsTr("API URL")
        onEditingFinished: bar.reconnectRequested()
    }

    TextField {
        id: apiKeyField

        Layout.fillWidth: true
        visible: bar.providers[providerCombo.currentIndex].needsKey
        echoMode: TextInput.Password
        placeholderText: qsTr("API Key")
        onEditingFinished: bar.reconnectRequested()
    }

    ComboBox {
        id: modelCombo

        Layout.preferredWidth: 200
        model: bar.controller.modelList
        editable: true
        enabled: !bar.controller.loadingModels
        displayText: bar.controller.loadingModels ? qsTr("Loading\u2026") : currentText
    }

    function providerName()  { return providerCombo.currentText }
    function providerUrl()   { return urlField.text.trim() }
    function providerKey()   { return apiKeyField.text.trim() }

    Component.onCompleted: {
        apiKeyField.text = bar.controller.envApiKey(providerCombo.currentText)
    }
}
