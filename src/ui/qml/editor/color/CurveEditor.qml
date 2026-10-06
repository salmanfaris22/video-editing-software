import QtQuick
import QtQuick.Controls.Basic

// Resolve-style custom curve: input along x, output up y, both 0…1.
// Click to add a point and drag it; drag points (⇧ = fine); double-click or
// right-click a point to remove it. The drawn curve is sampled from the
// engine (ProjectController.curveSamples), so it is exactly what renders.
Item {
    id: root

    property ProjectController project
    property var points: []          // [{x, y}] as stored (empty = no curve)
    property color tint: "#E6E8EE"   // channel color
    signal edited(var points)

    readonly property var shown: root.points.length >= 2 ? root.points : [{ x: 0, y: 0 }, { x: 1, y: 1 }]
    property int dragIndex: -1
    property int hoverIndex: -1

    function toPx(p) { return Qt.point(area.x + p.x * area.width, area.y + (1 - p.y) * area.height) }
    function fromPx(x, y) {
        return { x: Math.max(0, Math.min(1, (x - area.x) / area.width)), y: Math.max(0, Math.min(1, 1 - (y - area.y) / area.height)) }
    }
    function nearest(x, y) {
        let best = -1, dist = 9
        for (let i = 0; i < root.shown.length; ++i) {
            const p = toPx(root.shown[i])
            const d = Math.hypot(p.x - x, p.y - y)
            if (d < dist) { dist = d; best = i }
        }
        return best
    }
    function commit(list) { root.edited(list.slice().sort((a, b) => a.x - b.x)) }

    Rectangle {
        anchors.fill: parent
        color: "#0C0D11"
        border.color: "#262A33"
        radius: 2
    }
    Item {
        id: area
        anchors.fill: parent
        anchors.margins: 10
    }

    Canvas {
        id: plot
        anchors.fill: parent
        property var samples: root.project ? root.project.curveSamples(root.shown, 128) : []
        onSamplesChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const x0 = area.x, y0 = area.y, w = area.width, h = area.height
            // Grid (quarters, finer eighths) and the identity diagonal.
            for (let i = 1; i < 8; ++i) {
                ctx.strokeStyle = i % 2 === 0 ? "rgba(255,255,255,0.09)" : "rgba(255,255,255,0.04)"
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(x0 + w * i / 8, y0); ctx.lineTo(x0 + w * i / 8, y0 + h); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(x0, y0 + h * i / 8); ctx.lineTo(x0 + w, y0 + h * i / 8); ctx.stroke()
            }
            ctx.strokeStyle = "rgba(255,255,255,0.14)"
            ctx.setLineDash([4, 4])
            ctx.beginPath(); ctx.moveTo(x0, y0 + h); ctx.lineTo(x0 + w, y0); ctx.stroke()
            ctx.setLineDash([])
            if (samples.length < 2) return
            ctx.strokeStyle = root.tint
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let i = 0; i < samples.length; ++i) {
                const px = x0 + w * i / (samples.length - 1)
                const py = y0 + h * (1 - samples[i])
                if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
            }
            ctx.stroke()
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    Repeater {
        model: root.shown
        delegate: Rectangle {
            required property var modelData
            required property int index
            readonly property point p: root.toPx(modelData)
            readonly property bool hot: index === root.dragIndex || index === root.hoverIndex
            x: p.x - width / 2
            y: p.y - height / 2
            width: hot ? 11 : 9
            height: width
            radius: width / 2
            color: hot ? root.tint : "#0C0D11"
            border.color: root.tint
            border.width: 2
        }
    }

    Rectangle {  // in → out readout of the point under the hand
        visible: root.dragIndex >= 0 || root.hoverIndex >= 0
        readonly property var pt: root.shown[root.dragIndex >= 0 ? root.dragIndex : Math.max(0, root.hoverIndex)] || { x: 0, y: 0 }
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        width: readout.implicitWidth + 12
        height: 20
        radius: 3
        color: "#1A1D23"
        border.color: "#2E333D"
        Label {
            id: readout
            anchors.centerIn: parent
            text: "in " + Math.round(parent.pt.x * 1023) + "   out " + Math.round(parent.pt.y * 1023)
            color: Theme.text
            font.pixelSize: 10
            font.family: Theme.monoFamily
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: root.hoverIndex >= 0 ? Qt.SizeAllCursor : Qt.CrossCursor
        property point pressAt
        property var startPoint: ({ x: 0, y: 0 })
        onPositionChanged: mouse => {
            if (!pressed) { root.hoverIndex = root.nearest(mouse.x, mouse.y); return }
            if (root.dragIndex < 0) return
            const fine = mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1
            const target = root.fromPx(pressAt.x + (mouse.x - pressAt.x) * fine, pressAt.y + (mouse.y - pressAt.y) * fine)
            const list = root.shown.map(p => ({ x: p.x, y: p.y }))
            const last = list.length - 1
            // End points keep their x; others stay between their neighbours.
            let nx = target.x
            if (root.dragIndex === 0) nx = 0
            else if (root.dragIndex === last) nx = 1
            else nx = Math.max(list[root.dragIndex - 1].x + 0.012, Math.min(list[root.dragIndex + 1].x - 0.012, nx))
            list[root.dragIndex] = { x: nx, y: target.y }
            root.commit(list)
        }
        onExited: root.hoverIndex = -1
        onPressed: mouse => {
            const hit = root.nearest(mouse.x, mouse.y)
            const last = root.shown.length - 1
            if (mouse.button === Qt.RightButton) {
                if (hit > 0 && hit < last) root.commit(root.shown.filter((_, i) => i !== hit))
                return
            }
            if (hit >= 0) {
                root.dragIndex = hit
                const p = root.toPx(root.shown[hit])
                pressAt = Qt.point(p.x, p.y)
                return
            }
            // Add a point on the curve under the pointer and drag it.
            const at = root.fromPx(mouse.x, mouse.y)
            const samples = plot.samples
            const onCurve = samples.length > 1 ? samples[Math.round(at.x * (samples.length - 1))] : at.y
            const list = root.shown.map(p => ({ x: p.x, y: p.y }))
            list.push({ x: at.x, y: onCurve })
            list.sort((a, b) => a.x - b.x)
            root.dragIndex = list.findIndex(p => p.x === at.x)
            const p = root.toPx({ x: at.x, y: onCurve })
            pressAt = Qt.point(p.x, p.y)
            root.commit(list)
        }
        onReleased: root.dragIndex = -1
        onCanceled: root.dragIndex = -1
        onDoubleClicked: mouse => {
            const hit = root.nearest(mouse.x, mouse.y)
            if (hit > 0 && hit < root.shown.length - 1) root.commit(root.shown.filter((_, i) => i !== hit))
        }
    }
}
