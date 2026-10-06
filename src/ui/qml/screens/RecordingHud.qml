import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window

// Floating recording controls. A separate always-on-top window (not a child
// of the main window, which is minimized while recording). Lectern's own
// windows are excluded from screen capture, so the HUD never appears in a take.
// The window is exactly as large as its content: the control bar, plus a
// notice under it when something needs attention. Drag anywhere to move it;
// the chevron folds it down to the timer and the stop button.
Window {
    id: hud

    property RecorderController recorder
    property bool shown: false
    property bool compact: false
    readonly property bool paused: recorder && recorder.state === "paused"
    readonly property bool stopping: recorder && recorder.state === "stopping"
    property bool confirmDiscard: false

    readonly property var notices: {
        const list = []
        if (!hud.recorder) return list
        if (hud.recorder.presenterOverlay)
            list.push("Presenter Overlay is on: macOS draws your camera into the screen recording and the camera "
                      + "track stays blank. Turn it off under Video Effects (green camera icon in the menu bar).")
        if (hud.recorder.diskWarning.length > 0) list.push(hud.recorder.diskWarning)
        if (hud.recorder.droppedFrames > 0) list.push(hud.recorder.droppedFrames + " frames dropped")
        return list
    }

    visible: shown
    transientParent: null
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    width: Math.ceil(content.implicitWidth)
    height: Math.ceil(content.implicitHeight)
    x: Screen.virtualX + (Screen.width - 520) / 2
    y: Screen.virtualY + 40
    onShownChanged: confirmDiscard = false

    Column {
        id: content
        spacing: 6

        Rectangle {
            id: bar
            width: row.implicitWidth + row.anchors.leftMargin + row.anchors.rightMargin
            height: 56
            radius: height / 2
            color: Qt.rgba(0.07, 0.075, 0.095, 0.94)
            border.width: 1
            border.color: hud.notices.length > 0 ? Qt.rgba(0.96, 0.72, 0.24, 0.55) : Qt.rgba(1, 1, 1, 0.10)

            DragHandler {
                target: null
                onActiveChanged: if (active) hud.startSystemMove()
            }

            RowLayout {
                id: row
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 10
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 12
                    Layout.preferredHeight: 12
                    radius: 6
                    color: hud.paused ? Theme.warning : Theme.record
                    SequentialAnimation on opacity {
                        running: hud.visible && !hud.paused && !hud.stopping
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.35; duration: 700 }
                        NumberAnimation { from: 0.35; to: 1.0; duration: 700 }
                    }
                }
                Label {
                    text: hud.recorder ? hud.recorder.elapsedText : "0:00"
                    color: Theme.text
                    font.family: Theme.monoFamily
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                    Layout.minimumWidth: 58
                }
                Label {
                    text: hud.stopping ? "Saving…" : hud.paused ? "Paused" : ""
                    visible: text.length > 0
                    color: hud.paused ? Theme.warning : Theme.textMuted
                    font.pixelSize: Theme.fontS
                }
                Icon {
                    visible: hud.compact && hud.notices.length > 0
                    name: "warning"
                    size: 16
                    color: Theme.warning
                }
                ColumnLayout {
                    visible: !hud.compact
                    spacing: 4
                    LevelMeter {
                        Layout.preferredWidth: 64
                        implicitHeight: 5
                        segments: 11
                        level: hud.recorder ? hud.recorder.micLevel : 0
                        active: hud.recorder && hud.recorder.microphoneId.length > 0
                    }
                    LevelMeter {
                        Layout.preferredWidth: 64
                        implicitHeight: 5
                        segments: 11
                        level: hud.recorder ? hud.recorder.systemLevel : 0
                        active: hud.recorder && hud.recorder.systemAudio
                    }
                }
                IconButton {
                    visible: !hud.compact
                    iconName: hud.paused ? "play" : "pause"
                    tooltip: hud.paused ? "Resume (⌘⇧P)" : "Pause (⌘⇧P)"
                    enabled: !hud.stopping
                    onClicked: hud.recorder.togglePause()
                }
                AbstractButton {
                    id: stopButton
                    implicitWidth: stopRow.implicitWidth + 30
                    implicitHeight: 38
                    enabled: !hud.stopping
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: hud.recorder.stopRecording()
                    background: Rectangle {
                        radius: 19
                        color: stopButton.down ? "#E8394A" : stopButton.hovered ? Theme.recordHover : Theme.record
                        opacity: stopButton.enabled ? 1 : 0.5
                    }
                    contentItem: Item {
                        RowLayout {
                            id: stopRow
                            anchors.centerIn: parent
                            spacing: 7
                            Rectangle { Layout.preferredWidth: 10; Layout.preferredHeight: 10; radius: 2; color: "white" }
                            Label {
                                text: "Stop"
                                color: "white"
                                font.pixelSize: Theme.fontM
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: "Stop and open the editor (⌘⇧R)"
                    ToolTip.delay: 500
                }
                IconButton {
                    visible: !hud.compact
                    iconName: hud.confirmDiscard ? "check" : "close"
                    iconColor: hud.confirmDiscard ? Theme.danger : Theme.textMuted
                    tooltip: hud.confirmDiscard ? "Click again to discard this recording" : "Discard recording"
                    enabled: !hud.stopping
                    onClicked: {
                        if (hud.confirmDiscard) hud.recorder.cancelRecording()
                        else hud.confirmDiscard = true
                    }
                }
                IconButton {
                    implicitWidth: 24
                    iconSize: 14
                    iconName: hud.compact ? "chevron-right" : "chevron-left"
                    iconColor: Theme.textMuted
                    tooltip: hud.compact ? "Show all controls" : "Make smaller"
                    onClicked: hud.compact = !hud.compact
                }
            }
        }

        Repeater {
            model: hud.compact ? [] : hud.notices
            delegate: Rectangle {
                required property string modelData
                width: Math.max(bar.width, 320)
                height: noticeText.implicitHeight + 16
                radius: Theme.radiusM
                color: Qt.rgba(0.16, 0.12, 0.05, 0.95)
                border.width: 1
                border.color: Qt.rgba(0.96, 0.72, 0.24, 0.45)
                Icon {
                    id: noticeIcon
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.top: parent.top
                    anchors.topMargin: 9
                    name: "warning"
                    size: 15
                    color: Theme.warning
                }
                Label {
                    id: noticeText
                    anchors.left: noticeIcon.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData
                    color: Theme.text
                    font.pixelSize: Theme.fontS
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
