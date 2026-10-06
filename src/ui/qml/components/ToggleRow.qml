import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: root

    property string iconName
    property string title
    property string subtitle
    property bool checked
    signal toggled(bool checked)

    spacing: 10
    opacity: enabled ? 1 : 0.5

    Icon {
        visible: root.iconName.length > 0
        name: root.iconName
        size: 16
        color: Theme.textMuted
    }
    ColumnLayout {
        spacing: 0
        Layout.fillWidth: true
        Label {
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontM
        }
    }
    Switch {
        id: toggle
        focusPolicy: Qt.NoFocus
        checked: root.checked
        enabled: root.enabled
        onToggled: root.toggled(checked)
        indicator: Rectangle {
            implicitWidth: 36
            implicitHeight: 20
            x: toggle.leftPadding
            y: (toggle.height - height) / 2
            radius: 10
            color: toggle.checked ? Theme.accent : Theme.strokeStrong
            Behavior on color { ColorAnimation { duration: 120 } }
            Rectangle {
                width: 16
                height: 16
                radius: 8
                y: 2
                x: toggle.checked ? parent.width - width - 2 : 2
                color: "white"
                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
        }
    }
}
