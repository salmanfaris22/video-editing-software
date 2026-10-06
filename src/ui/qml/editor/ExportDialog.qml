import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Export settings → progress → result.
Popup {
    id: root

    property AppController app
    readonly property ExportController exporter: root.app.exporter
    property string resolution: "1080p"
    property int fps: 30
    property string quality: "high"
    property string format: "mp4"
    property real bitrateScale: 1.0
    property string path: ""
    readonly property var sizes: root.opened ? root.exporter.resolutions() : []
    readonly property var currentSize: {
        for (const s of root.sizes) if (s.id === root.resolution) return s
        return root.sizes.length > 1 ? root.sizes[1] : ({ width: 0, height: 0 })
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 480
    padding: 22
    closePolicy: root.exporter.running ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function localPath() {
        if (!root.path.length) return ""
        return root.path.startsWith("file:") ? decodeURIComponent(root.path.replace(/^file:\/\//, "")) : root.path
    }

    function syncPathExtension() {
        if (!root.path.length) {
            root.path = root.exporter.suggestedPath(root.format)
            return
        }
        const ext = root.format === "mkv" ? ".mkv" : root.format === "mov" ? ".mov" : ".mp4"
        const base = localPath().replace(/\.(mp4|mkv|mov)$/i, "")
        root.path = base + ext
    }

    onOpened: {
        if (!root.exporter.running) {
            root.exporter.reset()
            root.fps = Math.round(root.app.project.frameRate)
            if ([24, 25, 30, 50, 60].indexOf(root.fps) < 0) root.fps = 30
            root.path = root.exporter.suggestedPath(root.format)
        }
    }

    onFormatChanged: if (root.opened && !root.exporter.running) syncPathExtension()

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.width: 1
        border.color: Theme.strokeStrong
    }

    FileDialog {
        id: saveDialog
        title: "Export to"
        fileMode: FileDialog.SaveFile
        defaultSuffix: root.format
        nameFilters: [
            "MP4 video (*.mp4)",
            "Matroska video (*.mkv)",
            "QuickTime movie (*.mov)"
        ]
        onAccepted: {
            root.path = selectedFile.toString()
            const lower = root.path.toLowerCase()
            if (lower.endsWith(".mkv")) root.format = "mkv"
            else if (lower.endsWith(".mov")) root.format = "mov"
            else root.format = "mp4"
        }
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Icon { name: "export"; size: 20; color: Theme.accent }
            Label { Layout.fillWidth: true; text: "Export video"; color: Theme.text; font.pixelSize: Theme.fontXL; font.weight: Font.DemiBold }
        }

        // Settings
        ColumnLayout {
            Layout.fillWidth: true
            visible: !root.exporter.running && !root.exporter.finished && root.exporter.error.length === 0
            spacing: 10
            SectionLabel { text: "Format" }
            Segmented {
                Layout.fillWidth: true
                options: [
                    { label: "MP4", value: "mp4" },
                    { label: "MKV", value: "mkv" },
                    { label: "MOV", value: "mov" }
                ]
                value: root.format
                onSelected: value => root.format = value
            }
            SectionLabel { text: "Resolution" }
            Segmented {
                Layout.fillWidth: true
                options: root.sizes.map(s => ({ label: s.id, value: s.id }))
                value: root.resolution
                onSelected: value => root.resolution = value
            }
            Label {
                text: root.currentSize.width + " × " + root.currentSize.height + " · " + root.exporter.formatSummary(root.format)
                color: Theme.textFaint
                font.pixelSize: Theme.fontXS
            }
            SectionLabel { text: "Frame rate" }
            Segmented {
                Layout.fillWidth: true
                options: [{ label: "24", value: 24 }, { label: "25", value: 25 }, { label: "30", value: 30 },
                          { label: "50", value: 50 }, { label: "60", value: 60 }]
                value: root.fps
                onSelected: value => root.fps = value
            }
            SectionLabel { text: "Quality" }
            Segmented {
                Layout.fillWidth: true
                options: [
                    { label: "Draft", value: "draft" },
                    { label: "Smaller", value: "standard" },
                    { label: "High", value: "high" },
                    { label: "Best", value: "max" },
                    { label: "Ultra", value: "ultra" }
                ]
                value: root.quality
                onSelected: value => root.quality = value
            }
            SliderRow {
                label: "Bit rate"
                from: 0.5
                to: 2.0
                stepSize: 0.05
                defaultValue: 1.0
                value: root.bitrateScale
                format: v => Math.round(v * 100) + "%"
                onMoved: v => root.bitrateScale = v
            }
            Label {
                text: "About " + root.exporter.estimateMegabytes(root.resolution, root.fps, root.quality, root.bitrateScale).toFixed(0) + " MB"
                color: Theme.textFaint
                font.pixelSize: Theme.fontXS
            }
            SectionLabel { text: "Save to" }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: decodeURIComponent(root.path.replace(/^file:\/\//, ""))
                    color: Theme.text
                    font.pixelSize: Theme.fontS
                    elide: Text.ElideMiddle
                }
                PrimaryButton {
                    variant: "ghost"
                    implicitHeight: 32
                    text: "Change…"
                    onClicked: saveDialog.open()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    variant: "ghost"
                    text: "Cancel"
                    onClicked: root.close()
                }
                PrimaryButton {
                    text: "Export"
                    iconName: "export"
                    enabled: root.app.project.duration > 0
                    onClicked: root.exporter.start(root.resolution, root.fps, root.quality, root.path, root.format, root.bitrateScale)
                }
            }
        }

        // Progress
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.exporter.running
            spacing: 10
            Rectangle {
                Layout.fillWidth: true
                height: 8
                radius: 4
                color: Theme.raised
                Rectangle {
                    width: parent.width * root.exporter.progress
                    height: parent.height
                    radius: 4
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: 120 } }
                }
            }
            Label { text: root.exporter.status; color: Theme.textMuted; font.pixelSize: Theme.fontS }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    variant: "danger"
                    text: "Cancel export"
                    onClicked: root.exporter.cancel()
                }
            }
        }

        // Result
        ColumnLayout {
            Layout.fillWidth: true
            visible: !root.exporter.running && (root.exporter.finished || root.exporter.error.length > 0)
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: root.exporter.error.length > 0 ? root.exporter.error : root.exporter.status
                color: root.exporter.error.length > 0 ? Theme.danger : Theme.success
                font.pixelSize: Theme.fontM
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    visible: root.exporter.finished
                    variant: "ghost"
                    text: "Show in Finder"
                    iconName: "folder"
                    onClicked: root.exporter.reveal()
                }
                PrimaryButton {
                    text: root.exporter.finished ? "Done" : "Back"
                    onClicked: root.exporter.finished ? root.close() : root.exporter.reset()
                }
            }
        }
    }
}
