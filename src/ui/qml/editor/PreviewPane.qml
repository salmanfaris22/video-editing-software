import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The canvas as it will be exported (rendered by the playback engine with
// the shared compositor), the canvas tools (select, move, resize, edit text:
// CanvasOverlay) and the transport.
Rectangle {
    id: root

    property ProjectController project
    property PlaybackController playback
    signal activated()

    readonly property real aspect: root.project.canvasWidth / Math.max(1, root.project.canvasHeight)

    color: Theme.bg
    clip: true

    Item {
        id: stage
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: transport.top
        anchors.margins: 18

        Rectangle {
            id: canvas
            readonly property real fitWidth: Math.max(16, Math.min(stage.width, stage.height * root.aspect))
            width: fitWidth
            height: fitWidth / root.aspect
            anchors.centerIn: parent
            color: "#000000"
            clip: true

            EditorPreviewItem {
                id: preview
                anchors.fill: parent
                playback: root.playback
            }
            Label {
                anchors.centerIn: parent
                visible: !preview.hasFrame
                text: root.project.loaded ? "Rendering preview…" : ""
                color: Theme.textFaint
            }

        }

        // Outside the clipped canvas: a layer larger than the canvas keeps
        // its handles reachable around it.
        CanvasOverlay {
            objectName: "canvasOverlay"
            x: canvas.x
            y: canvas.y
            width: canvas.width
            height: canvas.height
            project: root.project
            playback: root.playback
            onActivated: root.activated()
            onEmptyClicked: root.playback.toggle()
        }
    }

    RowLayout {
        id: transport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        height: 48
        spacing: 6

        Label {
            text: root.project.formatTimecode(root.playback.position)
            color: Theme.text
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontM
            Layout.preferredWidth: 86
        }
        Label {
            text: "/ " + root.project.formatTime(root.project.duration)
            color: Theme.textFaint
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontS
        }
        Item { Layout.fillWidth: true }
        IconButton {
            iconName: "step-back"
            tooltip: "Previous frame (←)"
            enabled: root.project.duration > 0
            onClicked: { root.activated(); root.playback.step(-1) }
        }
        IconButton {
            implicitWidth: 42
            implicitHeight: 42
            iconSize: 22
            iconName: root.playback.playing ? "pause" : "play"
            tooltip: root.playback.playing ? "Pause (Space)" : "Play (Space)"
            enabled: root.project.duration > 0
            onClicked: { root.activated(); root.playback.toggle() }
        }
        IconButton {
            iconName: "step-forward"
            tooltip: "Next frame (→)"
            enabled: root.project.duration > 0
            onClicked: { root.activated(); root.playback.step(1) }
        }
        Item { Layout.fillWidth: true }
        ColumnLayout {
            spacing: 3
            LevelMeter { Layout.preferredWidth: 110; level: root.playback.levelLeft; active: root.playback.playing; segments: 18 }
            LevelMeter { Layout.preferredWidth: 110; level: root.playback.levelRight; active: root.playback.playing; segments: 18 }
        }
    }
}
