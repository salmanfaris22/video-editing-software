import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property string tool
    signal toolSelected(string tool)

    readonly property var tools: [
        { id: "library", label: "Library", icon: "folder" },
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

    implicitWidth: 52
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
                width: 44
                height: 44
                hoverEnabled: true
                onClicked: root.toolSelected(modelData.id)
                ToolTip.visible: hovered
                ToolTip.text: button.modelData.label
                ToolTip.delay: 400
                background: Rectangle {
                    radius: Theme.radiusM
                    color: button.current ? Theme.accentSoft : button.hovered ? Theme.hover : "transparent"
                }
                contentItem: Icon {
                    anchors.centerIn: parent
                    name: button.modelData.icon
                    size: 20
                    color: button.current ? Theme.accent : Theme.textMuted
                }
            }
        }
    }
    }
}
