import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Cutting: split, delete, remove a marked section, remove pauses, markers.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback
    property var editor  // EditorScreen: markIn / markOut

    readonly property var sel: root.project.selection
    readonly property bool hasRange: root.editor && root.editor.markIn >= 0 && root.editor.markOut > root.editor.markIn
    readonly property string linkedEditMode: root.editor ? root.editor.linkedEditMode : "track"
    readonly property bool cutAllTracks: root.linkedEditMode === "allTracks"

    contentWidth: availableWidth
    clip: true

    FileDialog {
        id: audioDialog
        title: "Add audio"
        nameFilters: ["Audio (*.mp3 *.m4a *.aac *.wav *.aif *.aiff *.flac *.ogg *.opus)"]
        onAccepted: root.project.importMedia(selectedFile, "music", root.playback.position)
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Label { text: "Cut"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PrimaryButton {
                Layout.fillWidth: true
                text: "Split"
                iconName: "cut"
                hint: "S"
                onClicked: root.project.splitAt(root.playback.position)
            }
            PrimaryButton {
                Layout.fillWidth: true
                variant: "danger"
                text: "Delete"
                iconName: "trash"
                hint: "⌫"
                enabled: root.project.selectedClip.length > 0 || root.project.selectedClips.length > 0
                onClicked: root.project.deleteSelected()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Segmented {
                Layout.fillWidth: true
                implicitHeight: 28
                options: [
                    { label: "This track", value: "track" },
                    { label: "All tracks", value: "allTracks" }
                ]
                value: root.linkedEditMode
                onSelected: v => { if (root.editor) root.editor.linkedEditMode = v }
            }
        }
        PrimaryButton {
            Layout.fillWidth: true
            variant: "ghost"
            text: root.project.busy ? root.project.busyText : "Add audio at playhead…"
            iconName: "audio"
            enabled: !root.project.busy && root.project.duration > 0
            onClicked: audioDialog.open()
        }

        SectionLabel { text: "Remove a section" }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PrimaryButton {
                Layout.fillWidth: true
                variant: "ghost"
                text: root.editor && root.editor.markIn >= 0 ? "Start " + root.project.formatTime(root.editor.markIn) : "Set start"
                hint: "I"
                onClicked: root.editor.markIn = root.playback.position
            }
            PrimaryButton {
                Layout.fillWidth: true
                variant: "ghost"
                text: root.editor && root.editor.markOut >= 0 ? "End " + root.project.formatTime(root.editor.markOut) : "Set end"
                hint: "O"
                onClicked: root.editor.markOut = root.playback.position
            }
        }
        PrimaryButton {
            Layout.fillWidth: true
            text: root.hasRange ? "Remove " + (root.editor.markOut - root.editor.markIn).toFixed(1) + " s" : "Remove section"
            enabled: root.hasRange
            onClicked: {
                const start = root.editor.markIn
                root.project.removeRange(root.editor.markIn, root.editor.markOut)
                root.editor.markIn = -1
                root.editor.markOut = -1
                root.playback.seek(start)
            }
        }

        SectionLabel { text: "Remove pauses" }
        SliderRow {
            id: threshold
            label: "Quieter than"
            from: -60; to: -20; stepSize: 1; defaultValue: -42
            value: -42
            format: v => Math.round(v) + " dB"
            onMoved: v => value = v
        }
        SliderRow {
            id: minPause
            label: "Longer than"
            from: 0.3; to: 3; defaultValue: 0.7
            value: 0.7
            format: v => v.toFixed(1) + " s"
            onMoved: v => value = v
        }
        SliderRow {
            id: padding
            label: "Keep around speech"
            from: 0; to: 0.5; defaultValue: 0.15
            value: 0.15
            format: v => Math.round(v * 1000) + " ms"
            onMoved: v => value = v
        }
        PrimaryButton {
            Layout.fillWidth: true
            variant: root.project.silences.length > 0 ? "ghost" : "accent"
            text: root.project.busy ? root.project.busyText : "Find pauses"
            iconName: "sparkle"
            enabled: !root.project.busy && root.project.duration > 0
            onClicked: root.project.findSilences(threshold.value, minPause.value, padding.value)
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.project.silences.length > 0
            spacing: 6
            Label {
                text: root.project.silences.length + " pauses · " + root.project.silenceTotal.toFixed(1) + " s"
                color: Theme.text
                font.pixelSize: Theme.fontS
            }
            RowLayout {
                Layout.fillWidth: true
                PrimaryButton {
                    Layout.fillWidth: true
                    text: "Remove all"
                    onClicked: root.project.removeSilences()
                }
                PrimaryButton {
                    variant: "ghost"
                    text: "Clear"
                    onClicked: root.project.clearSilences()
                }
            }
            Repeater {
                model: root.project.silences.slice(0, 40)
                delegate: Label {
                    required property var modelData
                    text: root.project.formatTime(modelData.start) + " · " + modelData.duration.toFixed(1) + " s"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontXS
                    font.family: Theme.monoFamily
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.playback.seek(parent.modelData.start)
                    }
                }
            }
        }

        SectionLabel { text: "Markers" }
        PrimaryButton {
            Layout.fillWidth: true
            variant: "ghost"
            text: "Add marker at playhead"
            iconName: "marker"
            hint: "M"
            onClicked: root.project.addMarker(root.playback.position, "")
        }
        Repeater {
            model: root.project.markers
            delegate: RowLayout {
                id: markerRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 6
                Rectangle { width: 8; height: 8; rotation: 45; color: markerRow.modelData.color }
                Label {
                    Layout.fillWidth: true
                    text: root.project.formatTime(markerRow.modelData.time) + "  " + markerRow.modelData.label
                    color: Theme.text
                    font.pixelSize: Theme.fontS
                    elide: Text.ElideRight
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.playback.seek(markerRow.modelData.time)
                    }
                }
                IconButton {
                    implicitWidth: 24
                    implicitHeight: 24
                    iconName: "close"
                    iconSize: 12
                    tooltip: "Remove marker"
                    onClicked: root.project.removeMarker(markerRow.modelData.id)
                }
            }
        }

        SectionLabel {
            visible: root.project.selectedClip.length > 0
            text: "Selected clip"
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.project.selectedClip.length > 0
            spacing: 4
            Label { text: root.sel.name || ""; color: Theme.text; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
            ToggleRow {
                Layout.fillWidth: true
                iconName: "eye"
                title: "Enabled"
                subtitle: "Disabled clips are hidden and silent"
                checked: root.sel.enabled === true
                onToggled: checked => root.project.setClipEnabled(root.project.selectedClip, checked)
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
