import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Primaries – Color Wheels (Resolve): Temp, Tint, Contrast, Pivot, Exposure
// above; Lift / Gamma / Gain / Offset wheels with master jogs; Color Boost,
// Shadows, Highlights, Saturation, Hue below. Edits the selected node: 01
// (the clip's correction) or a node that grades part of the picture.
Item {
    id: root
    objectName: "primariesPalette"

    property ProjectController project
    property var grade: ({})          // the selected node's values (selection keys)
    property bool editable: true
    signal valueEdited(string key, real value)
    signal wheelEdited(string wheel, real x, real y, real master)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        enabled: root.editable
        opacity: enabled ? 1 : 0.45

        RowLayout {  // Temp · Tint · Contrast · Pivot · Exposure
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 8
            spacing: 16
            ScrubField { objectName: "temp"; label: "Temp"; value: root.grade.temperature || 0; accent: "#F59E0B"; onEdited: v => root.valueEdited("temperature", v) }
            ScrubField { objectName: "tint"; label: "Tint"; value: root.grade.tint || 0; accent: "#D946EF"; onEdited: v => root.valueEdited("tint", v) }
            ScrubField { objectName: "contrast"; label: "Contrast"; value: root.grade.contrast || 0; onEdited: v => root.valueEdited("contrast", v) }
            ScrubField { objectName: "pivot"; label: "Pivot"; value: root.grade.pivot === undefined ? 0.5 : root.grade.pivot; from: 0; to: 1; defaultValue: 0.5; decimals: 3; onEdited: v => root.valueEdited("pivot", v) }
            ScrubField { objectName: "exposure"; label: "Exposure"; value: root.grade.exposure || 0; from: -2; to: 2; onEdited: v => root.valueEdited("exposure", v) }
            Item { Layout.fillWidth: true }
        }

        RowLayout {  // the four wheels share the width; the rows stay right under them
            id: wheels
            readonly property real dial: Math.max(70, Math.min((root.width - 16) / 4 - 30, root.height - 34 - 34 - 92, 200))
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: dial + 84
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 6
            spacing: 0
            Repeater {
                model: [
                    { wheel: "lift", title: "Lift", key: "lift" },
                    { wheel: "gamma", title: "Gamma", key: "gammaWheel" },
                    { wheel: "gain", title: "Gain", key: "gain" },
                    { wheel: "offset", title: "Offset", key: "offset" }
                ]
                delegate: Item {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColorWheel {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        width: parent.width
                        dialSize: wheels.dial
                        project: root.project
                        wheel: parent.modelData.wheel
                        title: parent.modelData.title
                        value: root.grade[parent.modelData.key] || ({ x: 0, y: 0, master: 0 })
                        onEdited: (x, y, m) => root.wheelEdited(parent.modelData.wheel, x, y, m)
                    }
                }
            }
        }

        RowLayout {  // Color Boost · Shadows · Highlights · Saturation · Hue
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 4
            spacing: 16
            ScrubField { objectName: "colorBoost"; label: "Color Boost"; value: root.grade.colorBoost || 0; accent: "#22D3EE"; onEdited: v => root.valueEdited("colorBoost", v) }
            ScrubField { objectName: "shadows"; label: "Shadows"; value: root.grade.shadows || 0; onEdited: v => root.valueEdited("shadows", v) }
            ScrubField { objectName: "highlights"; label: "Highlights"; value: root.grade.highlights || 0; accent: Theme.text; onEdited: v => root.valueEdited("highlights", v) }
            ScrubField { objectName: "saturation"; label: "Sat"; value: root.grade.saturation || 0; accent: "#A78BFA"; onEdited: v => root.valueEdited("saturation", v) }
            ScrubField { objectName: "hue"; label: "Hue"; value: root.grade.hue || 0; displayScale: 180; decimals: 0; accent: "#F472B6"; onEdited: v => root.valueEdited("hue", v) }
            Item { Layout.fillWidth: true }
        }
        Item { Layout.fillHeight: true }
    }
}
