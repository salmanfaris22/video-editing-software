import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string tool
    signal toolSelected(string tool)

    readonly property var tools: [
        { id: "setup", label: "Setup", icon: "setup" },
        { id: "layout", label: "Layout", icon: "layout" },
        { id: "cut", label: "Cut", icon: "cut" },
        { id: "effects", label: "Effects", icon: "effects" },
        { id: "overlay", label: "Overlay", icon: "overlay" },
        { id: "style", label: "Style", icon: "style" },
        { id: "subtitles", label: "Subtitles", icon: "subtitles" },
        { id: "audio", label: "Audio", icon: "audio" },
        { id: "adjust", label: "Adjust", icon: "adjust" }
    ]

    implicitWidth: 76
    color: Theme.surface

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.stroke
    }

    Flickable {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
        id: column
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 2
        Repeater {
            model: root.tools
            delegate: AbstractButton {
                id: button
                required property var modelData
                readonly property bool current: root.tool === modelData.id
                width: 64
                height: 54
                hoverEnabled: true
                onClicked: root.toolSelected(modelData.id)
                background: Rectangle {
                    radius: Theme.radiusM
                    color: button.current ? Theme.accentSoft : button.hovered ? Theme.hover : "transparent"
                }
                contentItem: Column {
                    spacing: 4
                    topPadding: 8
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: button.modelData.icon
                        size: 19
                        color: button.current ? Theme.accent : Theme.textMuted
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: button.modelData.label
                        color: button.current ? Theme.text : Theme.textMuted
                        font.pixelSize: 10
                        font.weight: button.current ? Font.DemiBold : Font.Normal
                    }
                }
            }
        }
    }
    }
}
