import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Window (Resolve's power windows): limit the selected node to a circle, a
// rectangle or a gradient (e.g. darken the sky). Drag the shape on the
// viewer, or set it here. On node 01, picking a shape adds a node with it.
Item {
    id: root
    objectName: "windowPalette"

    property ProjectController project
    property string clipId
    property var node: null
    signal nodeCreated(string nodeId)

    readonly property var w: root.node ? root.node.window : ({})
    readonly property string shape: root.w.shape || ""

    function setW(values) { if (root.node) root.project.setNodeWindow(root.clipId, root.node.id, values) }
    function choose(shape) {
        if (!root.node) {
            if (shape.length === 0) return
            const id = root.project.addNode(root.clipId, shape)
            if (id.length > 0) root.nodeCreated(id)
            return
        }
        root.setW({ shape: shape })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        RowLayout {
            spacing: 6
            Label { text: "Window"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
            Item { width: 8 }
            Repeater {
                model: [
                    { id: "", label: "None" },
                    { id: "circle", label: "Circle" },
                    { id: "rectangle", label: "Rectangle" },
                    { id: "gradient", label: "Gradient" }
                ]
                delegate: PaletteButton {
                    required property var modelData
                    objectName: "window-" + (modelData.id.length > 0 ? modelData.id : "none")
                    text: modelData.label
                    checkable: true
                    checked: !!root.node && root.shape === modelData.id
                    enabled: !!root.node || modelData.id.length > 0
                    tip: modelData.id === "gradient" ? "A soft fade across the picture (skies, floors)" : ""
                    onClicked: root.choose(modelData.id)
                }
            }
            Item { Layout.fillWidth: true }
            PaletteButton {
                objectName: "windowInvert"
                text: "Invert"
                checkable: true
                checked: root.w.invert === true
                enabled: root.shape.length > 0
                tip: "Change outside the window instead of inside"
                onClicked: root.setW({ invert: !(root.w.invert === true) })
            }
        }

        Label {
            visible: !root.node
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Pick a shape to grade only part of the picture: it is added as a new node after the clip's correction. Then drag it on the viewer."
            color: Theme.textMuted
            font.pixelSize: 11
        }

        GridLayout {
            visible: root.shape.length > 0
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 18
            rowSpacing: 8
            ScrubField { objectName: "windowX"; label: "Pan"; value: root.w.x === undefined ? 0.5 : root.w.x; from: -1; to: 2; defaultValue: 0.5; decimals: 3; onEdited: v => root.setW({ x: v }) }
            ScrubField { objectName: "windowY"; label: "Tilt"; value: root.w.y === undefined ? 0.5 : root.w.y; from: -1; to: 2; defaultValue: 0.5; decimals: 3; onEdited: v => root.setW({ y: v }) }
            ScrubField { objectName: "windowRotation"; label: "Rotate"; value: root.w.rotation || 0; from: -360; to: 360; defaultValue: 0; decimals: 1; dragStep: 0.5; onEdited: v => root.setW({ rotation: v }) }
            ScrubField { objectName: "windowWidth"; label: root.shape === "gradient" ? "Width" : "Size"; value: root.w.width || 0.5; from: 0.01; to: 4; defaultValue: 0.5; decimals: 3; onEdited: v => root.setW({ width: v }) }
            ScrubField { objectName: "windowHeight"; label: root.shape === "gradient" ? "Fade" : "Height"; value: root.w.height || 0.5; from: 0.01; to: 4; defaultValue: 0.5; decimals: 3; onEdited: v => root.setW({ height: v }) }
            ScrubField { objectName: "windowSoftness"; label: "Soft"; value: root.w.softness || 0; from: 0; to: 1; defaultValue: 0.2; decimals: 2; enabled: root.shape !== "gradient"; onEdited: v => root.setW({ softness: v }) }
        }
        Label {
            visible: !!root.node && root.shape.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "This node has no window: it changes the whole picture (or what its color key and person mask select)."
            color: Theme.textFaint
            font.pixelSize: 10
        }
        Label {
            visible: root.shape.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "On the viewer: drag inside to move · edge handles to resize · the top dot to rotate · ⇧ for fine moves."
            color: Theme.textFaint
            font.pixelSize: 10
        }
        Item { Layout.fillHeight: true }
    }
}
