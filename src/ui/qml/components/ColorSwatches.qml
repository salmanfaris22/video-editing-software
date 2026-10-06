import QtQuick
import QtQuick.Controls.Basic

// A row of color chips; "none" (empty string) shows as a crossed chip.
Flow {
    id: root

    property var colors: ["#FFFFFF", "#000000"]
    property string current
    property int chip: 24
    signal picked(string color)

    spacing: 6

    Repeater {
        model: root.colors
        delegate: Rectangle {
            id: chipItem
            required property string modelData
            readonly property bool selected: root.current.toLowerCase() === modelData.toLowerCase()
            width: root.chip
            height: root.chip
            radius: root.chip / 2
            color: modelData.length > 0 ? modelData : "transparent"
            border.width: selected ? 2 : 1
            border.color: selected ? Theme.accent : Theme.strokeStrong
            Rectangle {  // "none"
                visible: chipItem.modelData.length === 0
                anchors.centerIn: parent
                width: parent.width * 0.8
                height: 1.5
                rotation: -45
                color: Theme.danger
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.picked(chipItem.modelData)
            }
        }
    }
}
