import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Per-clip effects: zoom into a region, blur, vignette, opacity.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback

    readonly property var sel: root.project.selection
    readonly property bool visual: root.sel.visual === true
    readonly property string id: root.project.selectedClip

    function trackOf(role) {
        for (const track of root.project.tracks)
            for (const clip of track.clips)
                if (clip.role === role) return track.id
        return ""
    }

    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Label { text: "Effects"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }

        ColumnLayout {
            Layout.fillWidth: true
            visible: !root.visual
            spacing: 10
            PrimaryButton {
                Layout.fillWidth: true
                variant: "ghost"
                text: "Select the screen at the playhead"
                iconName: "screen"
                visible: root.trackOf("screen").length > 0
                onClicked: root.project.selectAt(root.playback.position, root.trackOf("screen"))
            }
            PrimaryButton {
                Layout.fillWidth: true
                variant: "ghost"
                text: "Select the camera at the playhead"
                iconName: "camera"
                visible: root.trackOf("camera").length > 0
                onClicked: root.project.selectAt(root.playback.position, root.trackOf("camera"))
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.visual
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: root.sel.name || ""; color: Theme.text; font.pixelSize: Theme.fontM; elide: Text.ElideRight }
                StatusChip { text: root.sel.role || ""; tone: "info" }
            }

            ToggleRow {
                Layout.fillWidth: true
                iconName: "sparkle"
                title: "Zoom"
                subtitle: "Magnify part of the picture"
                checked: root.sel.zoomOn === true
                onToggled: checked => root.project.setEffectEnabled(root.id, "zoom", checked)
            }
            SliderRow {
                visible: root.sel.zoomOn === true
                label: "Zoom"
                from: 1; to: 4; defaultValue: 1.5
                value: root.sel.zoom || 1
                format: v => v.toFixed(2) + "×"
                onMoved: v => root.project.setEffectValue(root.id, "zoom", "scale", v)
            }
            SliderRow {
                visible: root.sel.zoomOn === true
                label: "Focus left ↔ right"
                from: 0; to: 1; defaultValue: 0.5
                value: root.sel.zoomX === undefined ? 0.5 : root.sel.zoomX
                format: v => Math.round(v * 100) + "%"
                onMoved: v => root.project.setEffectValue(root.id, "zoom", "x", v)
            }
            SliderRow {
                visible: root.sel.zoomOn === true
                label: "Focus top ↕ bottom"
                from: 0; to: 1; defaultValue: 0.5
                value: root.sel.zoomY === undefined ? 0.5 : root.sel.zoomY
                format: v => Math.round(v * 100) + "%"
                onMoved: v => root.project.setEffectValue(root.id, "zoom", "y", v)
            }

            ToggleRow {
                Layout.fillWidth: true
                iconName: "effects"
                title: "Blur"
                subtitle: "Soften the picture (hide sensitive content)"
                checked: root.sel.blurOn === true
                onToggled: checked => root.project.setEffectEnabled(root.id, "blur", checked)
            }
            SliderRow {
                visible: root.sel.blurOn === true
                label: "Blur amount"
                from: 0; to: 1; defaultValue: 0.5
                value: root.sel.blur || 0
                format: v => Math.round(v * 100) + "%"
                onMoved: v => root.project.setEffectValue(root.id, "blur", "amount", v)
            }

            ToggleRow {
                Layout.fillWidth: true
                iconName: "adjust"
                title: "Vignette"
                subtitle: "Darken the edges"
                checked: root.sel.vignetteOn === true
                onToggled: checked => root.project.setEffectEnabled(root.id, "vignette", checked)
            }
            SliderRow {
                visible: root.sel.vignetteOn === true
                label: "Vignette strength"
                from: 0; to: 1; defaultValue: 0.5
                value: root.sel.vignette || 0
                format: v => Math.round(v * 100) + "%"
                onMoved: v => root.project.setEffectValue(root.id, "vignette", "amount", v)
            }

            KeyframesSection {
                Layout.fillWidth: true
                project: root.project
                playback: root.playback
                clipId: root.id
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
