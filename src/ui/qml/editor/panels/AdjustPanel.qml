import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Color: exposure, contrast, saturation, white balance — per clip, with presets.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback

    readonly property var sel: root.project.selection
    readonly property bool visual: root.sel.visual === true && root.sel.role !== "text"
    readonly property string id: root.project.selectedClip
    readonly property var looks: [
        { label: "Natural", values: {} },
        { label: "Vivid", values: { contrast: 0.15, saturation: 0.3 } },
        { label: "Warm", values: { temperature: 0.35, saturation: 0.1 } },
        { label: "Cool", values: { temperature: -0.35 } },
        { label: "Bright", values: { exposure: 0.35, contrast: 0.05 } },
        { label: "Mono", values: { saturation: -1, contrast: 0.1 } }
    ]
    readonly property var sliders: [
        { key: "exposure", label: "Exposure", from: -2, to: 2, unit: " EV" },
        { key: "brightness", label: "Brightness", from: -1, to: 1, unit: "" },
        { key: "contrast", label: "Contrast", from: -1, to: 1, unit: "" },
        { key: "saturation", label: "Saturation", from: -1, to: 1, unit: "" },
        { key: "temperature", label: "Temperature", from: -1, to: 1, unit: "" },
        { key: "tint", label: "Tint", from: -1, to: 1, unit: "" }
    ]

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

        Label { text: "Adjust"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }

        ColumnLayout {
            Layout.fillWidth: true
            visible: !root.visual
            spacing: 10
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
            SectionLabel { text: "Looks" }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                rowSpacing: 6
                columnSpacing: 6
                Repeater {
                    model: root.looks
                    delegate: PrimaryButton {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 32
                        variant: "ghost"
                        text: modelData.label
                        onClicked: root.project.setColorValues(root.id, modelData.values)
                    }
                }
            }
            SectionLabel { text: "Fine-tune" }
            Repeater {
                model: root.sliders
                delegate: SliderRow {
                    required property var modelData
                    label: modelData.label
                    from: modelData.from
                    to: modelData.to
                    defaultValue: 0
                    value: root.sel[modelData.key] || 0
                    format: v => (v > 0 ? "+" : "") + v.toFixed(2) + modelData.unit
                    onMoved: v => root.project.setColorValue(root.id, modelData.key, v)
                }
            }
            PrimaryButton {
                Layout.fillWidth: true
                variant: "ghost"
                text: "Reset"
                onClicked: root.project.resetColor(root.id)
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
