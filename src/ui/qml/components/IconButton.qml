import QtQuick
import QtQuick.Controls.Basic

AbstractButton {
    id: control

    property string iconName
    property real iconSize: 18
    property color iconColor: Theme.text
    property string tooltip
    property bool active: false

    implicitWidth: 34
    implicitHeight: 34
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: Theme.radiusS
        color: control.active ? Theme.accentSoft
             : control.down ? Theme.pressed
             : control.hovered && control.enabled ? Theme.hover
             : "transparent"
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: !control.enabled ? Theme.textFaint : control.active ? Theme.accent : control.iconColor
            opacity: control.enabled ? 1.0 : 0.6
        }
    }

    ToolTip.visible: hovered && tooltip.length > 0
    ToolTip.text: tooltip
    ToolTip.delay: 450
}
