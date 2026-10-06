import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Curves (Resolve): Custom — Y (luma, all channels first), R, G, B on top of
// the picture's histogram — and the HSL curves Hue vs Hue / Sat / Lum, Lum vs
// Sat, Sat vs Sat, Sat vs Lum. Click to add a point and drag it; double-click
// or right-click a point to remove it. Edits the selected node.
Item {
    id: root
    objectName: "curvesPalette"

    property ProjectController project
    property PlaybackController playback
    property var grade: ({})
    property bool editable: true
    property string channel: "y"
    /// "custom" or an HSL curve: hueVsHue, hueVsSat, hueVsLum, lumVsSat, satVsSat, satVsLum.
    property string mode: "custom"
    property bool picking: false
    signal curveEdited(string channel, var points)
    signal hslEdited(string curve, var points)
    signal pickRequested()

    readonly property var modes: [
        { id: "custom", name: "Custom" }, { id: "hueVsHue", name: "Hue vs Hue" }, { id: "hueVsSat", name: "Hue vs Sat" },
        { id: "hueVsLum", name: "Hue vs Lum" }, { id: "lumVsSat", name: "Lum vs Sat" }, { id: "satVsSat", name: "Sat vs Sat" },
        { id: "satVsLum", name: "Sat vs Lum" }
    ]
    readonly property bool hsl: root.mode !== "custom"
    /// A picked color (ProjectController.colorAt) becomes a point on the HSL curve.
    function addPicked(color) {
        if (!root.hsl || color.hue === undefined) return
        hslEditor.addAt(root.mode.indexOf("hue") === 0 ? color.hue : root.mode === "lumVsSat" ? color.lum : color.sat)
    }

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
            Rectangle { anchors.fill: parent; color: Theme.well; border.color: Theme.wellStroke; radius: Theme.radiusS }
            ScopeItem {  // the picture's histogram behind the custom curve
                anchors.fill: parent
                anchors.margins: 10
                visible: !root.hsl
                bare: true
                playback: root.playback
                opacity: 0.9
            }
            HslCurveEditor {
                id: hslEditor
                anchors.fill: parent
                visible: root.hsl
                project: root.project
                curve: root.hsl ? root.mode : "hueVsSat"
                points: root.hsl ? (root.grade[root.mode] || []) : []
                onEdited: pts => root.hslEdited(root.mode, pts)
            }
            CurveEditor {
                objectName: "curveEditor"
                anchors.fill: parent
                visible: !root.hsl
                project: root.project
                transparent: true
                points: root.grade[root.key] || []
                tint: root.channels.find(c => c.id === root.channel).color
                onEdited: pts => root.curveEdited(root.channel, pts)
            }
        }
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: false  // layouts fill by default; the curve takes the room
            Layout.preferredWidth: 150
            Layout.maximumWidth: 150
            spacing: 8
            DarkCombo {
                objectName: "curveMode"
                Layout.preferredWidth: 150
                model: root.modes.map(m => m.name)
                currentIndex: Math.max(0, root.modes.findIndex(m => m.id === root.mode))
                onActivated: index => root.mode = root.modes[index].id
            }
            ColumnLayout {  // HSL curve tools
                visible: root.hsl
                Layout.fillWidth: true
                spacing: 6
                Label {
                    Layout.preferredWidth: 150
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 10
                    text: root.mode === "hueVsHue" ? "Shift one hue toward its neighbours — e.g. move skin away from red."
                        : root.mode === "hueVsSat" ? "More or less saturation for one hue — e.g. calm the greens."
                        : root.mode === "hueVsLum" ? "Brighten or darken one hue — e.g. a deeper blue sky."
                        : root.mode === "lumVsSat" ? "Saturation by brightness — e.g. clean, gray shadows."
                        : root.mode === "satVsSat" ? "Change saturation by how saturated a color already is."
                        : "Brighten or darken colors by their saturation."
                }
                PaletteButton {
                    objectName: "sixVectors"
                    visible: root.mode.indexOf("hue") === 0
                    text: "Six vectors"
                    tip: "Points at red, yellow, green, cyan, blue and magenta to drag"
                    onClicked: hslEditor.addSixVectors()
                }
                PaletteButton {
                    objectName: "pickCurveColor"
                    text: root.picking ? "Click the picture…" : "Pick"
                    iconName: "sparkle"
                    checkable: true
                    checked: root.picking
                    tip: "Click a color in the viewer to add its point"
                    onClicked: root.pickRequested()
                }
                PaletteButton {
                    objectName: "resetHsl"
                    text: "Reset curve"
                    onClicked: root.hslEdited(root.mode, [])
                }
            }
            Label { visible: !root.hsl; text: "Channel"; color: Theme.textMuted; font.pixelSize: 11 }
            Row {
                visible: !root.hsl
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
                            color: chan.current ? Theme.selected : chan.hovered ? Theme.hover : Theme.raised
                            border.color: chan.current ? chan.modelData.color : Theme.strokeStrong
                        }
                        contentItem: Item {
                            Label { anchors.centerIn: parent; text: chan.modelData.id.toUpperCase(); color: chan.modelData.id === "y" ? Theme.text : chan.modelData.color; font.pixelSize: 12; font.weight: Font.Bold }
                            Rectangle { visible: chan.edited; width: 5; height: 5; radius: 2.5; color: chan.modelData.color; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4 }
                        }
                    }
                }
            }
            PaletteButton {
                visible: !root.hsl
                objectName: "resetChannel"
                text: "Reset " + root.channel.toUpperCase()
                onClicked: root.curveEdited(root.channel, [])
            }
            PaletteButton {
                visible: !root.hsl
                objectName: "resetCurves"
                text: "Reset all curves"
                onClicked: { for (const c of ["y", "r", "g", "b"]) root.curveEdited(c, []) }
            }
            Label {
                visible: !root.hsl
                Layout.preferredWidth: 150
                wrapMode: Text.WordWrap
                text: "Click the curve to add a point and drag it. Double-click or right-click a point to remove it. ⇧ for fine moves."
                color: Theme.textFaint
                font.pixelSize: 10
            }
        }
    }
}
