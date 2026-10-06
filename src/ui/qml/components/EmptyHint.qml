import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Shown when a panel needs a selection.
ColumnLayout {
    id: root
    property string iconName: "sparkle"
    property string text

    Layout.fillWidth: true
    spacing: 10
    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        width: 44
        height: 44
        radius: 22
        color: Theme.raised
        Icon {
            anchors.centerIn: parent
            name: root.iconName
            size: 20
            color: Theme.textMuted
        }
    }
    Label {
        Layout.fillWidth: true
        text: root.text
        color: Theme.textMuted
        font.pixelSize: Theme.fontS
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }
}
