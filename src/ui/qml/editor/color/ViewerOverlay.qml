import QtQuick
import QtQuick.Controls.Basic

// Drawn over the Color page viewer (sized to the picture):
// - the wipe divider (drag it) and Before / After labels;
// - the selected node's window with handles: drag inside to move, the edge
//   handles to resize, the top dot to rotate (⇧ = fine);
// - the color picker: click the picture to key the node on that color.
Item {
    id: root
    objectName: "viewerOverlay"

    property ProjectController project
    property PlaybackController playback
    property string clipId
    property var node: null
    property bool showWindow: false
    property bool picking: false
    /// "key": the click keys the node on that color; "curve": it reports the point (pickedAt) for an HSL curve.
    property string pickMode: "key"
    signal picked()
    signal pickedAt(real x, real y)

    // Where the clip's whole source frame lies on the canvas (fractions).
    property var frame: ({})
    function updateFrame() { root.frame = root.clipId.length > 0 ? root.project.sourceFrame(root.clipId, root.playback.position) : ({}) }
    onClipIdChanged: updateFrame()
    Connections {
        target: root.project
        function onProjectChanged() { root.updateFrame() }
        function onSnapshotChanged() { root.updateFrame() }
    }
    Connections {
        target: root.playback
        function onPositionChanged() { if (!root.playback.playing) root.updateFrame() }
    }
    Component.onCompleted: updateFrame()

    readonly property bool hasFrame: root.frame.w !== undefined && root.frame.w > 0

    // ---- Wipe / side by side ---------------------------------------------------
    Item {
        anchors.fill: parent
        visible: root.playback.compareMode === "wipe"
        Rectangle {
            id: divider
            x: root.playback.compareSplit * root.width - 1
            width: 2
            height: parent.height
            color: Theme.knob
            opacity: 0.9
        }
        Rectangle {
            objectName: "wipeHandle"
            x: divider.x - width / 2 + 1
            y: parent.height / 2 - height / 2
            width: 18
            height: 18
            radius: 9
            color: wipeDrag.pressed ? Theme.accent : Theme.knob
            border.color: "#0B0C10"
            border.width: 2
        }
        MouseArea {
            id: wipeDrag
            x: divider.x - 10
            width: 22
            height: parent.height
            cursorShape: Qt.SplitHCursor
            preventStealing: true
            onPositionChanged: mouse => {
                if (!pressed) return
                const p = mapToItem(root, mouse.x, mouse.y)
                root.playback.compareSplit = Math.max(0, Math.min(1, p.x / root.width))
            }
        }
        Label { x: 8; y: 8; text: root.playback.referenceStill.length > 0 ? "STILL" : "BEFORE"; color: "#F2F3F6"; font.pixelSize: 10; font.weight: Font.Bold; style: Text.Outline; styleColor: "#000000" }
        Label { x: parent.width - width - 8; y: 8; text: "AFTER"; color: "#F2F3F6"; font.pixelSize: 10; font.weight: Font.Bold; style: Text.Outline; styleColor: "#000000" }
    }
    Item {
        anchors.fill: parent
        visible: root.playback.compareMode === "side"
        Label { x: parent.width / 4 - width / 2; y: parent.height / 4 - height - 4; text: root.playback.referenceStill.length > 0 ? "STILL" : "BEFORE"; color: "#F2F3F6"; font.pixelSize: 10; font.weight: Font.Bold }
        Label { x: parent.width * 3 / 4 - width / 2; y: parent.height / 4 - height - 4; text: "AFTER"; color: "#F2F3F6"; font.pixelSize: 10; font.weight: Font.Bold }
    }
    Label {
        visible: root.playback.compareMode === "bypass"
        x: 8
        y: 8
        text: "GRADES OFF (⇧D)"
        color: Theme.warning
        font.pixelSize: 10
        font.weight: Font.Bold
        style: Text.Outline
        styleColor: "#000000"
    }

    // ---- Window handles ---------------------------------------------------------
    Item {
        id: windowLayer
        anchors.fill: parent
        visible: root.showWindow && root.hasFrame && !!root.node && (root.node.window.shape || "").length > 0
        // The layer's own rotation turns the source with it.
        transform: Rotation {
            origin.x: (root.frame.centerX || 0.5) * root.width
            origin.y: (root.frame.centerY || 0.5) * root.height
            angle: root.frame.rotation || 0
        }

        readonly property var w: root.node ? root.node.window : ({})
        readonly property bool mirror: root.frame.mirror === true
        // Window in canvas pixels.
        readonly property real cx: ((root.frame.x || 0) + (mirror ? 1 - (w.x || 0) : (w.x || 0)) * (root.frame.w || 0)) * root.width
        readonly property real cy: ((root.frame.y || 0) + (w.y || 0) * (root.frame.h || 0)) * root.height
        readonly property real hw: (w.width || 0) / 2 * (root.frame.w || 0) * root.width
        readonly property real hh: (w.height || 0) / 2 * (root.frame.h || 0) * root.height
        readonly property real angle: (mirror ? -1 : 1) * (w.rotation || 0)

        function set(values) { root.project.setNodeWindow(root.clipId, root.node.id, values) }
        // Canvas pixels → source fractions.
        function toSourceX(px) { const u = (px / root.width - root.frame.x) / root.frame.w; return mirror ? 1 - u : u }
        function toSourceY(py) { return (py / root.height - root.frame.y) / root.frame.h }

        Item {
            id: shapeItem
            x: windowLayer.cx - windowLayer.hw
            y: windowLayer.cy - windowLayer.hh
            width: windowLayer.hw * 2
            height: windowLayer.hh * 2
            rotation: windowLayer.angle

            Canvas {
                id: outline
                anchors.fill: parent
                anchors.margins: -40  // room for the soft edge
                property string shape: windowLayer.w.shape || ""
                property real soft: windowLayer.w.softness || 0
                onShapeChanged: requestPaint()
                onSoftChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const ox = 40, oy = 40, w = shapeItem.width, h = shapeItem.height
                    ctx.lineWidth = 1.5
                    if (shape === "gradient") {
                        // The fade: full at the top line, none at the bottom line.
                        ctx.strokeStyle = "rgba(255,255,255,0.9)"
                        ctx.beginPath(); ctx.moveTo(ox - 20, oy); ctx.lineTo(ox + w + 20, oy); ctx.stroke()
                        ctx.setLineDash([5, 4])
                        ctx.beginPath(); ctx.moveTo(ox - 20, oy + h / 2); ctx.lineTo(ox + w + 20, oy + h / 2); ctx.stroke()
                        ctx.setLineDash([2, 4])
                        ctx.beginPath(); ctx.moveTo(ox - 20, oy + h); ctx.lineTo(ox + w + 20, oy + h); ctx.stroke()
                        return
                    }
                    const draw = (scale) => {
                        const sw = w * scale, sh = h * scale
                        const x0 = ox + (w - sw) / 2, y0 = oy + (h - sh) / 2
                        ctx.beginPath()
                        if (shape === "circle") ctx.ellipse(x0, y0, sw, sh)
                        else ctx.rect(x0, y0, sw, sh)
                        ctx.stroke()
                    }
                    ctx.strokeStyle = "rgba(255,255,255,0.95)"
                    draw(1)
                    if (soft > 0.01) {
                        ctx.setLineDash([4, 4])
                        ctx.strokeStyle = "rgba(255,255,255,0.45)"
                        draw(1 - soft * 0.5)
                        draw(1 + soft * 0.5)
                    }
                }
            }
            // Move: drag inside.
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                property point pressAt
                property real startX
                property real startY
                onPressed: mouse => {
                    pressAt = mapToItem(root, mouse.x, mouse.y)
                    startX = windowLayer.w.x
                    startY = windowLayer.w.y
                }
                onPositionChanged: mouse => {
                    if (!pressed) return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const k = mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1
                    const du = (p.x - pressAt.x) * k / (root.frame.w * root.width) * (windowLayer.mirror ? -1 : 1)
                    const dv = (p.y - pressAt.y) * k / (root.frame.h * root.height)
                    windowLayer.set({ x: startX + du, y: startY + dv })
                }
            }
            // Resize: right, bottom and corner handles (the shape stays centered).
            Repeater {
                model: [
                    { id: "right", hx: 1, hy: 0.5 },
                    { id: "bottom", hx: 0.5, hy: 1 },
                    { id: "left", hx: 0, hy: 0.5 },
                    { id: "top", hx: 0.5, hy: 0 },
                    { id: "corner", hx: 1, hy: 1 }
                ]
                delegate: Rectangle {
                    id: handle
                    required property var modelData
                    objectName: "windowHandle-" + modelData.id
                    x: modelData.hx * shapeItem.width - width / 2
                    y: modelData.hy * shapeItem.height - height / 2
                    width: 10
                    height: 10
                    radius: modelData.id === "corner" ? 2 : 5
                    color: sizeDrag.pressed ? Theme.accent : Theme.knob
                    border.color: "#0B0C10"
                    visible: windowLayer.w.shape !== "gradient" || modelData.id === "top" || modelData.id === "bottom"
                    MouseArea {
                        id: sizeDrag
                        anchors.fill: parent
                        anchors.margins: -6
                        preventStealing: true
                        cursorShape: handle.modelData.hy === 0.5 ? Qt.SizeHorCursor : handle.modelData.hx === 0.5 ? Qt.SizeVerCursor : Qt.SizeFDiagCursor
                        property point pressAt
                        property real startW
                        property real startH
                        onPressed: mouse => {
                            pressAt = mapToItem(shapeItem, mouse.x, mouse.y)
                            startW = windowLayer.w.width
                            startH = windowLayer.w.height
                        }
                        onPositionChanged: mouse => {
                            if (!pressed) return
                            const p = mapToItem(shapeItem, mouse.x, mouse.y)  // in the shape's own (rotated) frame
                            const k = mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1
                            const m = handle.modelData
                            const values = {}
                            if (m.hx !== 0.5) {
                                const dx = (p.x - pressAt.x) * k * (m.hx === 0 ? -1 : 1)
                                values.width = Math.max(0.01, startW + 2 * dx / (root.frame.w * root.width))
                            }
                            if (m.hy !== 0.5) {
                                const dy = (p.y - pressAt.y) * k * (m.hy === 0 ? -1 : 1)
                                values.height = Math.max(0.01, startH + 2 * dy / (root.frame.h * root.height))
                            }
                            windowLayer.set(values)
                        }
                    }
                }
            }
            // Rotate: the dot above the shape.
            Rectangle {
                objectName: "windowRotate"
                x: shapeItem.width / 2 - width / 2
                y: -28
                width: 12
                height: 12
                radius: 6
                color: rotateDrag.pressed ? Theme.accent : Theme.knob
                border.color: "#0B0C10"
                Rectangle { x: parent.width / 2 - 0.5; y: parent.height; width: 1; height: 16; color: Theme.knob; opacity: 0.7 }
                MouseArea {
                    id: rotateDrag
                    anchors.fill: parent
                    anchors.margins: -6
                    preventStealing: true
                    cursorShape: Qt.CrossCursor
                    onPositionChanged: mouse => {
                        if (!pressed) return
                        const p = mapToItem(windowLayer, mouse.x, mouse.y)
                        let a = Math.atan2(p.x - windowLayer.cx, -(p.y - windowLayer.cy)) * 180 / Math.PI
                        if (mouse.modifiers & Qt.ShiftModifier) a = Math.round(a / 15) * 15
                        windowLayer.set({ rotation: windowLayer.mirror ? -a : a })
                    }
                }
            }
        }
    }

    // ---- Color picker -------------------------------------------------------------
    MouseArea {
        objectName: "pickArea"
        anchors.fill: parent
        visible: root.picking
        cursorShape: Qt.CrossCursor
        onClicked: mouse => {
            if (root.pickMode === "curve") {
                root.pickedAt(mouse.x / width, mouse.y / height)
                root.picked()
            } else if (root.node && root.project.pickNodeColor(root.clipId, root.node.id, mouse.x / width, mouse.y / height, root.playback.position)) {
                root.picked()
            }
        }
    }
    Rectangle {
        visible: root.picking
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
        width: pickHint.implicitWidth + 16
        height: 22
        radius: 11
        color: "#CC0B0C10"
        Label { id: pickHint; anchors.centerIn: parent; text: root.pickMode === "curve" ? "Click a color to add its point" : "Click the color to select"; color: Theme.knob; font.pixelSize: 11 }
    }
}
