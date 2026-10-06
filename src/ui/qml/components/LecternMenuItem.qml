import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Menu row with optional leading icon (for layer / add menus).
MenuItem {
    id: item

    property string iconName: ""
    property bool iconOnly: false

    implicitHeight: iconOnly ? 36 : 34
    implicitWidth: iconOnly ? 40 : undefined
    padding: 0
    leftPadding: iconOnly ? 0 : 8
    rightPadding: iconOnly ? 0 : 10

    ToolTip.visible: iconOnly && hovered
    ToolTip.text: item.text
    ToolTip.delay: 350

    background: Rectangle {
        radius: 0
        color: item.highlighted ? Theme.hover : "transparent"
    }

    contentItem: RowLayout {
        spacing: 8
        Layout.preferredWidth: item.iconOnly ? 40 : undefined
        Icon {
            Layout.alignment: Qt.AlignHCenter
            visible: item.iconName.length > 0
            name: item.iconName
            size: item.iconOnly ? 17 : 15
            color: item.enabled ? Theme.textMuted : Theme.textFaint
        }
        Label {
            visible: !item.iconOnly
            Layout.fillWidth: true
            text: item.text
            color: item.enabled ? Theme.textSecondary : Theme.textFaint
            font.pixelSize: Theme.fontS
            elide: Text.ElideRight
        }
    }
}
