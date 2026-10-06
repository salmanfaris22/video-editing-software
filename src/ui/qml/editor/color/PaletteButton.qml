import QtQuick
import QtQuick.Controls.Basic

// A compact Color page button: text (and optional icon), optional checked
// state with an accent edge, tooltip.
AbstractButton {
    id: control

    property string iconName
    property string tip
    property color accentColor: Theme.accent
    property bool compact: false

    implicitHeight: 24
    implicitWidth: row.implicitWidth + (compact ? 12 : 18)
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : 0.4

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 450

    background: Rectangle {
        radius: 3
        color: control.down ? Theme.pressed : control.checked ? "#262A33" : control.hovered ? Theme.hover : "#1A1D23"
        border.color: control.checked ? control.accentColor : "#30343E"
    }
    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 5
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconName.length > 0
                name: control.iconName
                size: 12
                color: control.checked ? Theme.text : Theme.textMuted
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.text.length > 0
                text: control.text
                color: control.checked ? Theme.text : "#C3C7D0"
                font.pixelSize: 11
                font.weight: control.checked ? Font.DemiBold : Font.Normal
            }
        }
    }
}
