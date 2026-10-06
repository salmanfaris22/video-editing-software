import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Curves – Custom (Resolve): Y (luma, all channels first), R, G, B on top
// of the picture's histogram. Click to add a point and drag it; double-click
// or right-click a point to remove it. Edits the selected node.
Item {
    id: root
    objectName: "curvesPalette"

    property ProjectController project
    property PlaybackController playback
    property var grade: ({})
    property bool editable: true
    property string channel: "y"
    signal curveEdited(string channel, var points)

    readonly property var channels: [
        { id: "y", color: "#E6E8EE" },
        { id: "r", color: "#F05252" },
        { id: "g", color: "#3FCB6A" },
        { id: "b", color: "#4D8DFF" }
    ]
    readonly property string key: "curve" + root.channel.toUpperCase()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12
        enabled: root.editable
        opacity: enabled ? 1 : 0.45

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.maximumWidth: height * 1.7
            Rectangle { anchors.fill: parent; color: "#0C0D11"; border.color: "#262A33"; radius: 2 }
            ScopeItem {  // the picture's histogram behind the curve
                anchors.fill: parent
                anchors.margins: 10
                bare: true
                playback: root.playback
                opacity: 0.9
            }
            CurveEditor {
                objectName: "curveEditor"
                anchors.fill: parent
                project: root.project
                transparent: true
                points: root.grade[root.key] || []
                tint: root.channels.find(c => c.id === root.channel).color
                onEdited: pts => root.curveEdited(root.channel, pts)
            }
        }
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 150
            spacing: 8
            Label { text: "Channel"; color: Theme.textMuted; font.pixelSize: 11 }
            Row {
                spacing: 4
                Repeater {
                    model: root.channels
                    delegate: AbstractButton {
                        id: chan
                        required property var modelData
                        objectName: "curve-" + modelData.id
                        readonly property bool current: root.channel === modelData.id
                        readonly property bool edited: (root.grade["curve" + modelData.id.toUpperCase()] || []).length >= 2
                        width: 32
                        height: 28
                        hoverEnabled: true
                        focusPolicy: Qt.NoFocus
                        onClicked: root.channel = modelData.id
                        background: Rectangle {
                            radius: 3
                            color: chan.current ? "#262A33" : chan.hovered ? "#1E2128" : "#16181D"
                            border.color: chan.current ? chan.modelData.color : "#2A2E37"
                        }
                        contentItem: Item {
                            Label { anchors.centerIn: parent; text: chan.modelData.id.toUpperCase(); color: chan.modelData.color; font.pixelSize: 12; font.weight: Font.Bold }
                            Rectangle { visible: chan.edited; width: 5; height: 5; radius: 2.5; color: chan.modelData.color; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4 }
                        }
                    }
                }
            }
            PaletteButton {
                objectName: "resetChannel"
                text: "Reset " + root.channel.toUpperCase()
                onClicked: root.curveEdited(root.channel, [])
            }
            PaletteButton {
                objectName: "resetCurves"
                text: "Reset all curves"
                onClicked: { for (const c of ["y", "r", "g", "b"]) root.curveEdited(c, []) }
            }
            Label {
                Layout.preferredWidth: 150
                wrapMode: Text.WordWrap
                text: "Click the curve to add a point and drag it. Double-click or right-click a point to remove it. ⇧ for fine moves."
                color: Theme.textFaint
                font.pixelSize: 10
            }
        }
    }
}
