import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Qualifier – HSL (Resolve): key one color so the selected node changes only
// that — a pen, a shirt, the sky, skin. Pick it on the viewer, then shape the
// hue, saturation and luminance ranges. Works on the selected node; on node
// 01 it offers to add a color-key node.
Item {
    id: root
    objectName: "qualifierPalette"

    property ProjectController project
    property PlaybackController playback
    property string clipId
    property var node: null          // the selected node's map, or null for node 01
    property bool picking: false
    property bool highlight: false
    signal pickRequested()
    signal nodeCreated(string nodeId)

    readonly property var q: root.node ? root.node.qualifier : ({})
    readonly property bool keyed: !!root.node && root.q.enabled === true

    function setQ(values) { if (root.node) root.project.setNodeQualifier(root.clipId, root.node.id, values) }

    // Node 01: explain and offer a node.
    ColumnLayout {
        anchors.centerIn: parent
        visible: !root.node
        spacing: 10
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 360
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: "Grade one color only — e.g. just the pen, a shirt or the sky. A color key works in its own node after the clip's correction."
            color: Theme.textMuted
            font.pixelSize: 11
        }
        PaletteButton {
            objectName: "addColorKey"
            Layout.alignment: Qt.AlignHCenter
            text: "Add color key node and pick"
            iconName: "plus"
            onClicked: {
                const id = root.project.addNode(root.clipId, "color")
                if (id.length > 0) { root.nodeCreated(id); root.pickRequested() }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        visible: !!root.node
        spacing: 8

        RowLayout {
            spacing: 6
            Label { text: "Qualifier – HSL"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            PaletteButton {
                objectName: "qualifierOn"
                text: root.keyed ? "Key on" : "Key off"
                checkable: true
                checked: root.keyed
                tip: "Use the color key for this node"
                onClicked: root.setQ({ enabled: !root.keyed })
            }
            PaletteButton {
                objectName: "pickColor"
                text: root.picking ? "Click the picture…" : "Pick"
                iconName: "sparkle"
                checkable: true
                checked: root.picking
                tip: "Pick the color to key on the viewer"
                onClicked: root.pickRequested()
            }
            PaletteButton {
                objectName: "qualifierInvert"
                text: "Invert"
                checkable: true
                checked: root.q.invert === true
                tip: "Key every color except this one"
                onClicked: root.setQ({ invert: !(root.q.invert === true) })
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 5
            columnSpacing: 10
            rowSpacing: 6
            enabled: root.keyed
            opacity: enabled ? 1 : 0.45

            // Hue
            Label { text: "Hue"; color: Theme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 32 }
            RangeBar {
                objectName: "hueRange"
                Layout.fillWidth: true
                wrap: true
                low: (root.q.hue || 0) - (root.q.hueWidth || 0)
                high: (root.q.hue || 0) + (root.q.hueWidth || 0)
                soft: root.q.hueSoft || 0
                active: (root.q.hueWidth || 0) < 0.5
                stops: [{ at: 0, color: "#FF3B3B" }, { at: 1 / 6, color: "#FFE53B" }, { at: 2 / 6, color: "#3BFF4E" },
                        { at: 3 / 6, color: "#3BF0FF" }, { at: 4 / 6, color: "#3B5BFF" }, { at: 5 / 6, color: "#FF3BEA" },
                        { at: 1, color: "#FF3B3B" }]
                onEdited: (lo, hi) => {
                    const c = (((lo + hi) / 2) % 1 + 1) % 1
                    root.setQ({ hue: c, hueWidth: Math.max(0.005, Math.min(0.5, (hi - lo) / 2)) })
                }
            }
            ScrubField { objectName: "hueCenter"; label: "Center"; value: root.q.hue || 0; from: 0; to: 1; displayScale: 360; decimals: 0; onEdited: v => root.setQ({ hue: v }) }
            ScrubField { objectName: "hueWidth"; label: "Width"; value: root.q.hueWidth || 0; from: 0; to: 0.5; defaultValue: 0.08; displayScale: 360; decimals: 0; onEdited: v => root.setQ({ hueWidth: v }) }
            ScrubField { objectName: "hueSoft"; label: "Soft"; value: root.q.hueSoft || 0; from: 0; to: 0.5; defaultValue: 0.04; displayScale: 360; decimals: 0; onEdited: v => root.setQ({ hueSoft: v }) }

            // Saturation
            Label { text: "Sat"; color: Theme.textMuted; font.pixelSize: 11 }
            RangeBar {
                objectName: "satRange"
                Layout.fillWidth: true
                low: root.q.satLow || 0
                high: root.q.satHigh === undefined ? 1 : root.q.satHigh
                soft: root.q.satSoft || 0
                stops: [{ at: 0, color: "#808080" }, { at: 1, color: "#FF3B3B" }]
                onEdited: (lo, hi) => root.setQ({ satLow: lo, satHigh: hi })
            }
            ScrubField { label: "Low"; value: root.q.satLow || 0; from: 0; to: 1; defaultValue: 0.15; displayScale: 100; decimals: 0; onEdited: v => root.setQ({ satLow: v }) }
            ScrubField { label: "High"; value: root.q.satHigh === undefined ? 1 : root.q.satHigh; from: 0; to: 1; defaultValue: 1; displayScale: 100; decimals: 0; onEdited: v => root.setQ({ satHigh: v }) }
            ScrubField { label: "Soft"; value: root.q.satSoft || 0; from: 0; to: 0.5; defaultValue: 0.05; displayScale: 100; decimals: 0; onEdited: v => root.setQ({ satSoft: v }) }

            // Luminance
            Label { text: "Lum"; color: Theme.textMuted; font.pixelSize: 11 }
            RangeBar {
                objectName: "lumRange"
                Layout.fillWidth: true
                low: root.q.lumLow || 0
                high: root.q.lumHigh === undefined ? 1 : root.q.lumHigh
                soft: root.q.lumSoft || 0
                stops: [{ at: 0, color: "#000000" }, { at: 1, color: "#FFFFFF" }]
                onEdited: (lo, hi) => root.setQ({ lumLow: lo, lumHigh: hi })
            }
            ScrubField { label: "Low"; value: root.q.lumLow || 0; from: 0; to: 1; defaultValue: 0; displayScale: 100; decimals: 0; onEdited: v => root.setQ({ lumLow: v }) }
            ScrubField { label: "High"; value: root.q.lumHigh === undefined ? 1 : root.q.lumHigh; from: 0; to: 1; defaultValue: 1; displayScale: 100; decimals: 0; onEdited: v => root.setQ({ lumHigh: v }) }
            ScrubField { label: "Soft"; value: root.q.lumSoft || 0; from: 0; to: 0.5; defaultValue: 0.05; displayScale: 100; decimals: 0; onEdited: v => root.setQ({ lumSoft: v }) }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.keyed ? "Tip: turn on Highlight (⇧H) to see what is keyed; widen Soft for smooth edges, and add a window to limit the key to one place."
                             : "Turn the key on, then click Pick and click the color in the viewer."
            color: Theme.textFaint
            font.pixelSize: 10
        }
        Item { Layout.fillHeight: true }
    }
}
