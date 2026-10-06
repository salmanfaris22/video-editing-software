import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// How the selected text enters and leaves (fade, slide, pop, zoom,
// typewriter, wipe, blur), how long each takes, and a preview button.
ColumnLayout {
    id: root

    property ProjectController project
    property PlaybackController playback
    readonly property var sel: root.project.selection

    spacing: 8

    function preview(fromEnd) {
        // Play the entrance from the clip start, or the exit up to its end.
        const start = root.sel.start || 0
        const duration = root.sel.duration || 0
        const lead = fromEnd ? Math.max(start, start + duration - (root.sel.outDuration || 0) - 0.6) : Math.max(0, start - 0.2)
        root.playback.seek(lead)
        root.playback.play()
        stopper.interval = Math.round(1000 * (fromEnd ? start + duration - lead + 0.3
                                                      : (root.sel.inDuration || 0) + 0.9))
        stopper.restart()
    }

    Timer {
        id: stopper
        onTriggered: root.playback.pause()
    }

    SectionLabel { text: "Animation" }

    Repeater {
        model: [
            { key: "animationIn", durationKey: "inDuration", label: "In", fromEnd: false },
            { key: "animationOut", durationKey: "outDuration", label: "Out", fromEnd: true }
        ]
        delegate: ColumnLayout {
            id: phase
            required property var modelData
            Layout.fillWidth: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                FieldLabel { text: phase.modelData.label; Layout.fillWidth: true }
                IconButton {
                    implicitWidth: 26
                    implicitHeight: 26
                    iconSize: 14
                    iconName: "play"
                    tooltip: "Preview the " + (phase.modelData.fromEnd ? "exit" : "entrance")
                    onClicked: root.preview(phase.modelData.fromEnd)
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.project.textAnimations
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool current: root.sel[phase.modelData.key] === modelData.id
                        width: chipLabel.implicitWidth + 18
                        height: 26
                        radius: 13
                        color: chip.current ? Theme.accentSoft : chipMouse.containsMouse ? Theme.hover : Theme.raised
                        border.width: 1
                        border.color: chip.current ? Theme.accent : Theme.stroke
                        Label {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: chip.modelData.name
                            color: chip.current ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fontXS
                        }
                        MouseArea {
                            id: chipMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.project.setTextValue(root.project.selectedClip, phase.modelData.key, chip.modelData.id)
                                if (chip.modelData.id !== "none") root.preview(phase.modelData.fromEnd)
                            }
                        }
                    }
                }
            }
            SliderRow {
                visible: root.sel[phase.modelData.key] !== "none"
                label: phase.modelData.label + " duration"
                from: 0.1; to: 2.0; defaultValue: 0.4
                value: root.sel[phase.modelData.durationKey] || 0.4
                format: v => v.toFixed(1) + " s"
                onMoved: v => root.project.setTextValue(root.project.selectedClip, phase.modelData.durationKey, v)
            }
        }
    }
}
