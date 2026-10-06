import QtQuick
import QtQuick.Controls.Basic

// Light / dark switch (sun · moon), saved in the workspace.
Rectangle {
    id: root

    property AppController app

    implicitWidth: row.implicitWidth + 8
    implicitHeight: 34
    radius: Theme.radiusM
    color: Theme.raised
    border.color: Theme.stroke

    function choose(mode) {
        Theme.apply(mode)
        if (root.app) root.app.setWorkspaceValue("theme", mode)
        console.info("theme:", mode)
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: [{ mode: "light", icon: "sun", tip: "Light mode" }, { mode: "dark", icon: "moon", tip: "Dark mode" }]
            delegate: AbstractButton {
                id: option
                required property var modelData
                objectName: "theme-" + modelData.mode
                readonly property bool current: Theme.mode === modelData.mode
                width: 30
                height: 26
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                onClicked: root.choose(modelData.mode)
                ToolTip.visible: hovered
                ToolTip.text: modelData.tip
                ToolTip.delay: 500
                background: Rectangle {
                    radius: Theme.radiusS
                    color: option.current ? Theme.accent : option.hovered ? Theme.hover : "transparent"
                }
                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        name: option.modelData.icon
                        size: 15
                        color: option.current ? Theme.accentText : Theme.textMuted
                    }
                }
            }
        }
    }
}
