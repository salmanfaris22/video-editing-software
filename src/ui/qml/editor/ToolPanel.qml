import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The context panel for the tool chosen in the rail. Each tool is its own
// file under editor/panels; they only display state and call the controller.
Rectangle {
    id: root

    property string tool
    property ProjectController project
    property PlaybackController playback
    property var editor

    color: Theme.surface

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.stroke
    }

    Loader {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        anchors.topMargin: 14
        sourceComponent: {
            switch (root.tool) {
            case "setup": return setupPanel
            case "cut": return cutPanel
            case "effects": return effectsPanel
            case "overlay": return overlayPanel
            case "style": return stylePanel
            case "subtitles": return subtitlesPanel
            case "audio": return audioPanel
            case "adjust": return adjustPanel
            default: return layoutPanel
            }
        }
    }

    Component { id: setupPanel; SetupPanel { project: root.project; playback: root.playback } }
    Component { id: layoutPanel; LayoutPanel { project: root.project; playback: root.playback } }
    Component { id: cutPanel; CutPanel { project: root.project; playback: root.playback; editor: root.editor } }
    Component { id: effectsPanel; EffectsPanel { project: root.project; playback: root.playback } }
    Component { id: overlayPanel; OverlayPanel { project: root.project; playback: root.playback } }
    Component { id: stylePanel; StylePanel { project: root.project; playback: root.playback } }
    Component { id: subtitlesPanel; SubtitlesPanel { project: root.project; playback: root.playback } }
    Component { id: audioPanel; AudioPanel { project: root.project; playback: root.playback } }
    Component { id: adjustPanel; AdjustPanel { project: root.project; playback: root.playback } }
}
