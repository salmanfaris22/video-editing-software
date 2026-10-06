import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Images, logos and extra videos placed freely on the canvas.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback

    readonly property var sel: root.project.selection
    property var editor
    readonly property bool isOverlay: root.sel.role === "overlay"
    readonly property var xf: (root.project.tracks, root.project.clipTransformAt(root.project.selectedClip, root.playback.position))
    readonly property bool keyframeMode: root.editor ? root.editor.keyframeAtPlayhead : true
    readonly property var overlays: {
        const out = []
        for (const track of root.project.tracks)
            for (const clip of track.clips)
                if (clip.role === "overlay") out.push(clip)
        return out
    }

    contentWidth: availableWidth
    clip: true

    FileDialog {
        id: imageDialog
        title: "Add an image or logo"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.tif *.tiff *.heic)"]
        onAccepted: root.project.importMedia(selectedFile, "overlay", root.playback.position)
    }
    FileDialog {
        id: videoDialog
        title: "Add a video"
        nameFilters: ["Videos (*.mp4 *.mov *.m4v *.mkv *.webm *.avi)"]
        onAccepted: root.project.importMedia(selectedFile, "overlay", root.playback.position)
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Label { text: "Overlay"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PrimaryButton {
                Layout.fillWidth: true
                text: "Image or logo"
                iconName: "image"
                enabled: !root.project.busy
                onClicked: imageDialog.open()
            }
            PrimaryButton {
                Layout.fillWidth: true
                variant: "ghost"
                text: "Video"
                iconName: "plus"
                enabled: !root.project.busy
                onClicked: videoDialog.open()
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.isOverlay
            spacing: 8
            SectionLabel { text: "Selected overlay" }
            Label { text: root.sel.name || ""; color: Theme.text; font.pixelSize: Theme.fontM; elide: Text.ElideRight; Layout.fillWidth: true }
            SliderRow {
                label: "Size"
                from: 0.05; to: 1.2; defaultValue: 0.3
                value: root.xf.scale !== undefined ? root.xf.scale : (root.sel.scale || 0.3)
                format: v => Math.round(v * 100) + "% width"
                onMoved: v => {
                    if (root.keyframeMode && root.xf.onClip)
                        root.project.setClipKeyframe(root.project.selectedClip, "scale", root.playback.position, v, true)
                    else
                        root.project.setClipScale(root.project.selectedClip, v)
                }
            }
            SliderRow {
                label: "Horizontal position"
                from: 0; to: 1; defaultValue: 0.5
                value: root.xf.x !== undefined ? root.xf.x : (root.sel.x === undefined ? 0.5 : root.sel.x)
                format: v => Math.round(v * 100) + "%"
                onMoved: v => {
                    if (root.keyframeMode && root.xf.onClip)
                        root.project.setClipKeyframe(root.project.selectedClip, "positionX", root.playback.position, v, true)
                    else
                        root.project.setClipPosition(root.project.selectedClip, v, root.xf.y !== undefined ? root.xf.y : root.sel.y)
                }
            }
            SliderRow {
                label: "Vertical position"
                from: 0; to: 1; defaultValue: 0.5
                value: root.xf.y !== undefined ? root.xf.y : (root.sel.y === undefined ? 0.5 : root.sel.y)
                format: v => Math.round(v * 100) + "%"
                onMoved: v => {
                    if (root.keyframeMode && root.xf.onClip)
                        root.project.setClipKeyframe(root.project.selectedClip, "positionY", root.playback.position, v, true)
                    else
                        root.project.setClipPosition(root.project.selectedClip, root.xf.x !== undefined ? root.xf.x : root.sel.x, v)
                }
            }
            SliderRow {
                label: "Opacity"
                from: 0; to: 1; defaultValue: 1
                value: root.xf.opacity !== undefined ? root.xf.opacity : (root.sel.opacity === undefined ? 1 : root.sel.opacity)
                format: v => Math.round(v * 100) + "%"
                onMoved: v => {
                    if (root.keyframeMode && root.xf.onClip)
                        root.project.setClipKeyframe(root.project.selectedClip, "opacity", root.playback.position, v, true)
                    else
                        root.project.setClipOpacity(root.project.selectedClip, v)
                }
            }
            SliderRow {
                label: "Duration"
                from: 0.5; to: Math.max(10, (root.sel.duration || 1) * 2); defaultValue: 5
                value: root.sel.duration || 1
                format: v => v.toFixed(1) + " s"
                onMoved: v => root.project.setClipTiming(root.project.selectedClip, root.sel.start, v)
            }
            PrimaryButton {
                Layout.fillWidth: true
                variant: "danger"
                text: "Remove overlay"
                iconName: "trash"
                onClicked: root.project.deleteSelected()
            }
        }

        SectionLabel { text: root.overlays.length > 0 ? "On the timeline" : "" }
        Repeater {
            model: root.overlays
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool current: root.project.selectedClip === modelData.id
                Layout.fillWidth: true
                implicitHeight: 32
                radius: Theme.radiusS
                color: current ? Theme.accentSoft : rowMouse.containsMouse ? Theme.hover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    Icon { name: "image"; size: 14; color: Theme.textMuted }
                    Label { Layout.fillWidth: true; text: row.modelData.name; color: Theme.text; font.pixelSize: Theme.fontS; elide: Text.ElideRight }
                    Label { text: root.project.formatTime(row.modelData.start); color: Theme.textFaint; font.pixelSize: Theme.fontXS; font.family: Theme.monoFamily }
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.project.selectClip(row.modelData.id)
                        root.playback.seek(row.modelData.start)
                    }
                }
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
