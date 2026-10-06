import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root

    property string text
    // ok | warn | error | off | info
    property string tone: "ok"

    readonly property color toneColor: tone === "ok" ? Theme.success
        : tone === "warn" ? Theme.warning
        : tone === "error" ? Theme.danger
        : tone === "info" ? Theme.accent
        : Theme.textFaint

    implicitHeight: 22
    implicitWidth: label.implicitWidth + 24
    radius: 11
    color: Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.12)

    Rectangle {
        id: dot
        width: 6
        height: 6
        radius: 3
        color: root.toneColor
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
    }
    Label {
        id: label
        anchors.left: dot.right
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: root.tone === "off" ? Theme.textMuted : root.toneColor
        font.pixelSize: Theme.fontXS
        font.weight: Font.DemiBold
    }
}
