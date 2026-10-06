import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string text
    property string actionText
    // info | warn | error
    property string tone: "info"
    signal action()
    signal dismissed()

    readonly property color toneColor: tone === "warn" ? Theme.warning : tone === "error" ? Theme.danger : Theme.accent

    visible: text.length > 0
    implicitHeight: 44
    radius: Theme.radiusM
    color: Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.10)
    border.width: 1
    border.color: Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.35)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 6
        spacing: 10
        Icon {
            name: root.tone === "info" ? "sparkle" : "warning"
            size: 16
            color: root.toneColor
        }
        Label {
            Layout.fillWidth: true
            text: root.text
            color: Theme.text
            font.pixelSize: Theme.fontM
            elide: Text.ElideRight
        }
        PrimaryButton {
            visible: root.actionText.length > 0
            text: root.actionText
            variant: "ghost"
            implicitHeight: 30
            onClicked: root.action()
        }
        IconButton {
            iconName: "close"
            iconSize: 14
            implicitWidth: 28
            implicitHeight: 28
            onClicked: root.dismissed()
        }
    }
}
