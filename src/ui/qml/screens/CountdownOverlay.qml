import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: root

    property RecorderController recorder

    color: Qt.rgba(0.03, 0.04, 0.06, 0.88)

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true  // swallow input while counting down
    }

    Column {
        anchors.centerIn: parent
        spacing: 22

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 190
            height: 190
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 6
                border.color: Theme.recordSoft
            }
            Rectangle {
                id: pulse
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                radius: width / 2
                color: "transparent"
                border.width: 6
                border.color: Theme.record
                SequentialAnimation on scale {
                    running: root.visible
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 1.06; duration: 500; easing.type: Easing.OutQuad }
                    NumberAnimation { from: 1.06; to: 1.0; duration: 500; easing.type: Easing.InQuad }
                }
            }
            Label {
                anchors.centerIn: parent
                text: root.recorder ? root.recorder.countdown : ""
                color: Theme.text
                font.pixelSize: 96
                font.weight: Font.Bold
            }
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Recording starts…"
            color: Theme.text
            font.pixelSize: Theme.fontXL
            font.weight: Font.DemiBold
        }
        PrimaryButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Cancel"
            hint: "Esc"
            variant: "ghost"
            onClicked: root.recorder.cancelRecording()
        }
    }
}
