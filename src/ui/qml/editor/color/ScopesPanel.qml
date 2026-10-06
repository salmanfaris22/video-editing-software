import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Scopes (Resolve): parade, waveform, vectorscope or histogram of the graded
// picture, 0–1023 scale; 2-up adds a vectorscope beside the main scope.
Rectangle {
    id: root
    objectName: "scopesPanel"

    property PlaybackController playback
    property var editor
    property bool twoUp: false
    readonly property alias stats: scope.stats
    readonly property alias mode: scope.mode

    color: "#121419"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 6
            spacing: 6
            Label { text: "Scopes"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Segmented {
                objectName: "scopeMode"
                implicitHeight: 24
                Layout.minimumWidth: implicitWidth
                options: [
                    { label: "Parade", value: "parade" },
                    { label: "Wave", value: "waveform" },
                    { label: "Vector", value: "vectorscope" },
                    { label: "Hist", value: "histogram" }
                ]
                value: scope.mode
                onSelected: v => {
                    scope.mode = v
                    if (root.editor) root.editor.scopeMode = v
                }
            }
            PaletteButton {
                objectName: "scopes2up"
                compact: true
                text: "2-up"
                checkable: true
                checked: root.twoUp
                tip: "Add a vectorscope beside the main scope"
                onClicked: root.twoUp = !root.twoUp
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            ScopeItem {
                id: scope
                objectName: "scope"
                Layout.fillWidth: true
                Layout.fillHeight: true
                playback: root.playback
                mode: root.editor && root.editor.scopeMode ? root.editor.scopeMode : "parade"
            }
            ScopeItem {
                visible: root.twoUp
                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(parent.height, parent.width * 0.45)
                playback: root.playback
                mode: scope.mode === "vectorscope" ? "waveform" : "vectorscope"
            }
        }
    }
}
