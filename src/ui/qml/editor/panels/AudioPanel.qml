import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Sound: a mixer for every track with audio, clip fades and gain, music.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback

    readonly property var sel: root.project.selection
    readonly property var audioTracks: root.project.tracks.filter(t => t.hasAudio)

    function db(v) { return (v > 0 ? "+" : "") + v.toFixed(1) + " dB" }

    contentWidth: availableWidth
    clip: true

    FileDialog {
        id: musicDialog
        title: "Add background music"
        nameFilters: ["Audio (*.mp3 *.m4a *.aac *.wav *.aif *.aiff *.flac *.ogg *.opus)"]
        onAccepted: root.project.importMedia(selectedFile, "music", root.playback.position)
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Label { text: "Audio"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                spacing: 3
                LevelMeter { Layout.preferredWidth: 180; level: root.playback.levelLeft; active: root.playback.playing }
                LevelMeter { Layout.preferredWidth: 180; level: root.playback.levelRight; active: root.playback.playing }
            }
            Label {
                Layout.fillWidth: true
                text: root.playback.outputName.length > 0 ? root.playback.outputName : "Press play to hear it"
                color: Theme.textFaint
                font.pixelSize: Theme.fontXS
                elide: Text.ElideRight
            }
        }

        SectionLabel { text: "Mixer" }
        Repeater {
            model: root.audioTracks
            delegate: Rectangle {
                id: strip
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: column.implicitHeight + 16
                radius: Theme.radiusM
                color: Theme.raised
                border.width: 1
                border.color: Theme.stroke
                ColumnLayout {
                    id: column
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: strip.modelData.name; color: Theme.text; font.pixelSize: Theme.fontS; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        IconButton {
                            implicitWidth: 28
                            implicitHeight: 28
                            iconSize: 14
                            iconName: strip.modelData.muted ? "speaker-off" : "speaker"
                            iconColor: strip.modelData.muted ? Theme.danger : Theme.textMuted
                            active: strip.modelData.muted
                            tooltip: strip.modelData.muted ? "Unmute track" : "Mute track"
                            onClicked: root.project.setTrackValue(strip.modelData.id, "muted", !strip.modelData.muted)
                        }
                        AbstractButton {
                            implicitWidth: 28
                            implicitHeight: 28
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            onClicked: root.project.setTrackValue(strip.modelData.id, "solo", !strip.modelData.solo)
                            ToolTip.visible: hovered
                            ToolTip.text: "Solo: hear only this track"
                            background: Rectangle {
                                radius: Theme.radiusS
                                color: strip.modelData.solo ? Theme.warningSoft : parent.hovered ? Theme.hover : "transparent"
                            }
                            contentItem: Label {
                                text: "S"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: strip.modelData.solo ? Theme.warning : Theme.textMuted
                                font.pixelSize: Theme.fontS
                                font.weight: Font.Bold
                            }
                        }
                    }
                    SliderRow {
                        label: "Volume"
                        from: -30; to: 12; defaultValue: 0
                        value: strip.modelData.gainDb
                        format: v => root.db(v)
                        onMoved: v => root.project.setTrackValue(strip.modelData.id, "gainDb", v)
                    }
                }
            }
        }
        PrimaryButton {
            Layout.fillWidth: true
            variant: "ghost"
            text: root.project.busy ? root.project.busyText : "Add background music…"
            iconName: "music"
            enabled: !root.project.busy
            onClicked: musicDialog.open()
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.sel.hasAudio === true
            spacing: 8
            SectionLabel { text: "Selected clip" }
            Label { text: root.sel.name || ""; color: Theme.text; font.pixelSize: Theme.fontM; elide: Text.ElideRight; Layout.fillWidth: true }
            SliderRow {
                label: "Clip volume"
                from: -30; to: 12; defaultValue: 0
                value: root.sel.gainDb || 0
                format: v => root.db(v)
                onMoved: v => root.project.setClipAudio(root.project.selectedClip, "gainDb", v)
            }
            SliderRow {
                label: "Fade in"
                from: 0; to: Math.max(0.5, Math.min(5, (root.sel.duration || 1) / 2)); defaultValue: 0
                value: root.sel.fadeIn || 0
                format: v => v.toFixed(1) + " s"
                onMoved: v => root.project.setClipAudio(root.project.selectedClip, "fadeIn", v)
            }
            SliderRow {
                label: "Fade out"
                from: 0; to: Math.max(0.5, Math.min(5, (root.sel.duration || 1) / 2)); defaultValue: 0
                value: root.sel.fadeOut || 0
                format: v => v.toFixed(1) + " s"
                onMoved: v => root.project.setClipAudio(root.project.selectedClip, "fadeOut", v)
            }
            ToggleRow {
                Layout.fillWidth: true
                iconName: "speaker-off"
                title: "Mute this clip"
                checked: root.sel.muted === true
                onToggled: checked => root.project.setClipAudio(root.project.selectedClip, "muted", checked)
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
