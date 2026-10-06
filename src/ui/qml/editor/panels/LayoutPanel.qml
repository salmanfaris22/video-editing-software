import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// How screen and camera share the canvas — for the whole video or from the
// playhead on — plus the screen frame and camera styling.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback
    property bool fromPlayhead: false

    // Re-evaluated when the playhead moves or the document changes.
    readonly property string current: (root.project.layoutRegions, root.project.layoutAt(root.playback.position))
    readonly property real canvasAspect: root.project.canvasWidth / Math.max(1, root.project.canvasHeight)
    // The screen and camera layers at the playhead (re-evaluated on every edit).
    readonly property string screenClip: (root.project.tracks, root.project.style, root.current,
                                          root.project.roleClipAt("screen", root.playback.position))
    readonly property string cameraClip: (root.project.tracks, root.project.style, root.current,
                                          root.project.roleClipAt("camera", root.playback.position))
    readonly property var screenBox: root.screenClip.length > 0
        ? (root.project.tracks, root.project.style, root.project.layerBox(root.screenClip, root.playback.position)) : ({})
    readonly property var cameraBox: root.cameraClip.length > 0
        ? (root.project.tracks, root.project.style, root.project.layerBox(root.cameraClip, root.playback.position)) : ({})
    readonly property bool customized: (root.project.style, root.current, root.project.layoutCustomized(root.playback.position))

    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Label { text: "Layout"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }
        Segmented {
            Layout.fillWidth: true
            options: [{ label: "Whole video", value: false }, { label: "From playhead", value: true }]
            value: root.fromPlayhead
            onSelected: value => root.fromPlayhead = value
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 8
            Repeater {
                model: root.project.layoutPresets
                delegate: Rectangle {
                    id: tile
                    required property var modelData
                    readonly property bool isCurrent: root.current === modelData.id
                    Layout.fillWidth: true
                    implicitHeight: mini.height + 34
                    radius: Theme.radiusM
                    color: tileMouse.containsMouse ? Theme.hover : Theme.raised
                    border.width: isCurrent ? 2 : 1
                    border.color: isCurrent ? Theme.accent : Theme.stroke
                    Rectangle {
                        id: mini
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width - 16, 76 * Math.min(1.8, root.canvasAspect))
                        height: width / root.canvasAspect
                        radius: 3
                        color: "#0D0F14"
                        Rectangle {
                            visible: !!tile.modelData.screen
                            x: tile.modelData.screen ? tile.modelData.screen.x * mini.width : 0
                            y: tile.modelData.screen ? tile.modelData.screen.y * mini.height : 0
                            width: tile.modelData.screen ? tile.modelData.screen.w * mini.width : 0
                            height: tile.modelData.screen ? tile.modelData.screen.h * mini.height : 0
                            radius: 2
                            color: "#2C3550"
                        }
                        Rectangle {
                            visible: !!tile.modelData.camera
                            readonly property real side: tile.modelData.camera ? Math.min(tile.modelData.camera.w * mini.width, tile.modelData.camera.h * mini.height) : 0
                            x: tile.modelData.camera ? (tile.modelData.circle ? tile.modelData.camera.x * mini.width + (tile.modelData.camera.w * mini.width - side) / 2 : tile.modelData.camera.x * mini.width) : 0
                            y: tile.modelData.camera ? (tile.modelData.circle ? tile.modelData.camera.y * mini.height + (tile.modelData.camera.h * mini.height - side) / 2 : tile.modelData.camera.y * mini.height) : 0
                            width: tile.modelData.camera ? (tile.modelData.circle ? side : tile.modelData.camera.w * mini.width) : 0
                            height: tile.modelData.camera ? (tile.modelData.circle ? side : tile.modelData.camera.h * mini.height) : 0
                            radius: tile.modelData.circle ? width / 2 : 3
                            color: "#2E6B57"
                        }
                    }
                    Label {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 7
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: tile.modelData.name + (tile.modelData.customized ? " ·" : "")
                        color: tile.isCurrent ? Theme.text : Theme.textMuted
                        font.pixelSize: Theme.fontXS
                    }
                    Rectangle {
                        visible: tile.modelData.customized === true
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 6
                        width: 7
                        height: 7
                        radius: 3.5
                        color: Theme.accent
                        ToolTip.visible: tileMouse.containsMouse
                        ToolTip.text: "Arranged by hand for this canvas size"
                    }
                    MouseArea {
                        id: tileMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.fromPlayhead ? root.project.setLayoutFrom(tile.modelData.id, root.playback.position)
                                                     : root.project.setLayoutPreset(tile.modelData.id)
                    }
                }
            }
        }

        SectionLabel {
            visible: root.project.layoutRegions.length > 1
            text: "Layout sections"
        }
        Repeater {
            model: root.project.layoutRegions.length > 1 ? root.project.layoutRegions : []
            delegate: Rectangle {
                id: section
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 28
                radius: Theme.radiusS
                color: sectionMouse.containsMouse ? Theme.hover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    Label {
                        text: root.project.formatTime(section.modelData.start) + "–"
                              + root.project.formatTime(section.modelData.start + section.modelData.duration)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontXS
                        font.family: Theme.monoFamily
                    }
                    Label { Layout.fillWidth: true; text: section.modelData.name; color: Theme.text; font.pixelSize: Theme.fontS; elide: Text.ElideRight }
                }
                MouseArea {
                    id: sectionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.playback.seek(section.modelData.start)
                }
            }
        }

        SectionLabel { text: "Arrange" }
        SliderRow {
            visible: root.screenBox.w !== undefined
            label: "Screen size"
            from: 0.2; to: 3.0; defaultValue: 1
            value: root.screenBox.w || 1
            format: v => Math.round(v * 100) + "%"
            onMoved: v => root.project.setLayerWidth(root.screenClip, root.playback.position, v)
        }
        SliderRow {
            visible: root.cameraBox.w !== undefined
            label: "Camera size"
            from: 0.08; to: 1.0; defaultValue: 0.3
            value: root.cameraBox.w || 0.3
            format: v => Math.round(v * 100) + "%"
            onMoved: v => root.project.setLayerWidth(root.cameraClip, root.playback.position, v)
        }
        PrimaryButton {
            Layout.fillWidth: true
            variant: "ghost"
            enabled: root.customized
            text: root.customized ? "Reset arrangement" : "Arranged as the preset"
            onClicked: {
                root.project.resetLayoutCustomization(root.playback.position)
                root.project.setRoleTransform("camera", 0.5, 0.5, 1.0)
            }
        }

        SectionLabel { text: "Screen frame" }
        SliderRow {
            label: "Padding"
            from: 0; to: 0.2; defaultValue: 0
            value: root.project.style.screenPadding || 0
            format: v => Math.round(v * 100) + "%"
            onMoved: v => root.project.setStyleValue("screenPadding", v)
        }
        SliderRow {
            label: "Rounded corners"
            from: 0; to: 0.06; defaultValue: 0
            value: root.project.style.screenRadius || 0
            format: v => Math.round(v * 1000) / 10 + "%"
            onMoved: v => root.project.setStyleValue("screenRadius", v)
        }
        SliderRow {
            label: "Shadow"
            from: 0; to: 1; defaultValue: 0
            value: root.project.style.screenShadow || 0
            format: v => Math.round(v * 100) + "%"
            onMoved: v => root.project.setStyleValue("screenShadow", v)
        }

        SectionLabel { text: "Camera" }
        Segmented {
            Layout.fillWidth: true
            options: [{ label: "Square", value: "rect" }, { label: "Rounded", value: "rounded" }, { label: "Circle", value: "circle" }]
            value: root.project.style.cameraShape || "rounded"
            onSelected: value => root.project.setStyleValue("cameraShape", value)
        }
        SliderRow {
            label: "Border"
            from: 0; to: 0.012; defaultValue: 0
            value: root.project.style.cameraBorder || 0
            format: v => (v * 1000).toFixed(1)
            onMoved: v => root.project.setStyleValue("cameraBorder", v)
        }
        ColorSwatches {
            Layout.fillWidth: true
            visible: (root.project.style.cameraBorder || 0) > 0
            colors: ["#FFFFFF", "#000000", "#7C8CFF", "#F5C142", "#34D399", "#FF4D5E"]
            current: root.project.style.cameraBorderColor || "#FFFFFF"
            onPicked: color => root.project.setStyleValue("cameraBorderColor", color)
        }
        ToggleRow {
            Layout.fillWidth: true
            iconName: "camera"
            title: "Mirror camera"
            subtitle: "Flip the camera picture horizontally"
            checked: root.project.style.cameraMirror === true
            onToggled: checked => root.project.setStyleValue("cameraMirror", checked)
        }
        Item { Layout.preferredHeight: 8 }
    }
}
