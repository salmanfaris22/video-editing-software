import QtQuick
import QtQuick.Controls.Basic

// Resolve's HSL curves: Hue vs Hue / Sat / Lum, Lum vs Sat, Sat vs Sat and
// Sat vs Lum. x is hue (wrapping around red), luminance or saturation; y 0.5
// (the dashed line) changes nothing. Click the line to add a point — the
// first one gets two anchors around it so only nearby colors change — and
// drag it (⇧ = fine); double-click or right-click a point to remove it. The
// drawn curve is sampled from the engine, so it is exactly what renders.
Item {
    id: root
    objectName: "hslCurveEditor"

    property ProjectController project
    property string curve: "hueVsSat"
    property var points: []
    signal edited(var points)

    readonly property bool periodic: root.curve.indexOf("hue") === 0
    readonly property var samples: root.project ? root.project.hslCurveSamples(root.points, root.curve, 181) : []
    property int dragIndex: -1
    property int hoverIndex: -1

    function clamp01(v) { return Math.max(0, Math.min(1, v)) }
    function toPx(p) { return Qt.point(area.x + p.x * area.width, area.y + (1 - p.y) * area.height) }
    function fromPx(x, y) { return { x: clamp01((x - area.x) / area.width), y: clamp01(1 - (y - area.y) / area.height) } }
    function valueAt(x) { return root.samples.length > 1 ? root.samples[Math.round(clamp01(x) * (root.samples.length - 1))] : 0.5 }
    function nearest(x, y) {
        let best = -1, dist = 9
        for (let i = 0; i < root.points.length; ++i) {
            const p = toPx(root.points[i])
            const d = Math.hypot(p.x - x, p.y - y)
            if (d < dist) { dist = d; best = i }
        }
        return best
    }
    function sorted(list) { return list.slice().sort((a, b) => a.x - b.x) }
    /// The points with a new one at x on the curve (anchors first when the curve is empty); returns {list, index}.
    function withPointAt(x) {
        let list = root.points.map(p => ({ x: p.x, y: p.y }))
        if (list.length < 2) {
            list = []
            if (root.periodic) {
                list.push({ x: ((x - 0.12) % 1 + 1) % 1, y: 0.5 })
                list.push({ x: (x + 0.12) % 1, y: 0.5 })
            } else {
                list.push({ x: 0, y: 0.5 })
                list.push({ x: 1, y: 0.5 })
            }
        }
        const point = { x: x, y: root.valueAt(x) }
        list = sorted(list.filter(p => Math.abs(p.x - x) >= 0.01).concat([point]))
        return { list: list, index: list.indexOf(point) }
    }
    /// Adds a point at x (a picked color), anchors included; the editors reselect it.
    function addAt(x) {
        const r = root.withPointAt(clamp01(x))
        root.edited(r.list)
    }
    /// Points at the six vectors (red, yellow, green, cyan, blue, magenta) for hue curves.
    function addSixVectors() {
        const list = root.points.map(p => ({ x: p.x, y: p.y }))
        for (let k = 0; k < 6; ++k) {
            const x = k / 6
            if (!list.some(p => Math.abs(p.x - x) < 0.02)) list.push({ x: x, y: root.valueAt(x) })
        }
        root.edited(sorted(list))
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.well
        border.color: Theme.wellStroke
        radius: Theme.radiusS
    }
    Item {
        id: area
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 10
        anchors.bottomMargin: 22  // room for the axis band
    }

    Canvas {
        id: plot
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const x0 = area.x, y0 = area.y, w = area.width, h = area.height
            // Axis band: what x means.
            const band = ctx.createLinearGradient(x0, 0, x0 + w, 0)
            if (root.periodic) {
                const hues = ["#FF3B3B", "#FFE53B", "#3BFF4E", "#3BF0FF", "#3B5BFF", "#FF3BEA", "#FF3B3B"]
                for (let i = 0; i < hues.length; ++i) band.addColorStop(i / 6, hues[i])
            } else if (root.curve === "lumVsSat") {
                band.addColorStop(0, "#000000"); band.addColorStop(1, "#FFFFFF")
            } else {
                band.addColorStop(0, "#7A7A7A"); band.addColorStop(1, "#FF3B3B")
            }
            ctx.fillStyle = band
            ctx.fillRect(x0, y0 + h + 6, w, 8)
            // Grid: the six vectors for hue, quarters otherwise.
            const steps = root.periodic ? 6 : 4
            ctx.lineWidth = 1
            ctx.strokeStyle = "rgba(255,255,255,0.07)"
            for (let i = 1; i < steps; ++i) {
                ctx.beginPath(); ctx.moveTo(x0 + w * i / steps, y0); ctx.lineTo(x0 + w * i / steps, y0 + h); ctx.stroke()
            }
            for (let i = 1; i < 4; ++i) {
                if (i === 2) continue
                ctx.beginPath(); ctx.moveTo(x0, y0 + h * i / 4); ctx.lineTo(x0 + w, y0 + h * i / 4); ctx.stroke()
            }
            // Neutral line.
            ctx.strokeStyle = "rgba(255,255,255,0.28)"
            ctx.setLineDash([4, 4])
            ctx.beginPath(); ctx.moveTo(x0, y0 + h / 2); ctx.lineTo(x0 + w, y0 + h / 2); ctx.stroke()
            ctx.setLineDash([])
            const s = root.samples
            if (s.length < 2) return
            // The change, filled toward the neutral line, then the curve.
            ctx.beginPath()
            ctx.moveTo(x0, y0 + h / 2)
            for (let i = 0; i < s.length; ++i) ctx.lineTo(x0 + w * i / (s.length - 1), y0 + h * (1 - s[i]))
            ctx.lineTo(x0 + w, y0 + h / 2)
            ctx.closePath()
            ctx.fillStyle = "rgba(124,140,255,0.12)"
            ctx.fill()
            ctx.strokeStyle = "#E6E8EE"
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let i = 0; i < s.length; ++i) {
                const px = x0 + w * i / (s.length - 1), py = y0 + h * (1 - s[i])
                if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
            }
            ctx.stroke()
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onSamplesChanged() { plot.requestPaint() }
            function onCurveChanged() { plot.requestPaint() }
        }
    }

    Repeater {
        model: root.points
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
            color: hot ? "#E6E8EE" : Theme.well
            border.color: "#E6E8EE"
            border.width: 2
        }
    }

    Rectangle {  // what the point under the hand does
        objectName: "hslReadout"
        visible: root.dragIndex >= 0 || root.hoverIndex >= 0
        readonly property var pt: root.points[root.dragIndex >= 0 ? root.dragIndex : Math.max(0, root.hoverIndex)] || { x: 0, y: 0.5 }
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
            color: "#E6E8EE"
            font.pixelSize: 10
            font.family: Theme.monoFamily
            text: {
                const p = parent.pt
                const axis = root.periodic ? "Hue " + Math.round(p.x * 360) + "°"
                           : (root.curve === "lumVsSat" ? "Lum " : "Sat ") + Math.round(p.x * 100) + "%"
                const v = root.curve === "hueVsHue" ? ((p.y - 0.5) * 360 >= 0 ? "+" : "") + Math.round((p.y - 0.5) * 360) + "°"
                        : root.curve === "hueVsLum" || root.curve === "satVsLum" ? ((p.y - 0.5) >= 0 ? "+" : "") + (p.y - 0.5).toFixed(2)
                        : "×" + (2 * p.y).toFixed(2)
                return axis + "   " + v
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: root.hoverIndex >= 0 ? Qt.SizeAllCursor : Qt.CrossCursor
        property point pressAt
        onPositionChanged: mouse => {
            if (!pressed) { root.hoverIndex = root.nearest(mouse.x, mouse.y); return }
            if (root.dragIndex < 0) return
            const fine = mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1
            const target = root.fromPx(pressAt.x + (mouse.x - pressAt.x) * fine, pressAt.y + (mouse.y - pressAt.y) * fine)
            const list = root.points.map(p => ({ x: p.x, y: p.y }))
            const i = root.dragIndex
            if (i < 0 || i >= list.length) return
            let nx = target.x
            if (!root.periodic && (i === 0 || i === list.length - 1)) nx = list[i].x  // the ends stay at 0 and 1
            const lo = i > 0 ? list[i - 1].x + 0.012 : 0
            const hi = i < list.length - 1 ? list[i + 1].x - 0.012 : 1
            list[i] = { x: Math.max(lo, Math.min(hi, nx)), y: target.y }
            root.edited(list)
        }
        onExited: root.hoverIndex = -1
        onPressed: mouse => {
            const hit = root.nearest(mouse.x, mouse.y)
            if (mouse.button === Qt.RightButton) {
                if (hit >= 0) root.edited(root.points.filter((_, i) => i !== hit))
                return
            }
            if (hit >= 0) {
                root.dragIndex = hit
                const p = root.toPx(root.points[hit])
                pressAt = Qt.point(p.x, p.y)
                return
            }
            const at = root.fromPx(mouse.x, mouse.y)
            const r = root.withPointAt(at.x)
            root.dragIndex = r.index
            const p = root.toPx(r.list[r.index])
            pressAt = Qt.point(p.x, p.y)
            root.edited(r.list)
        }
        onReleased: root.dragIndex = -1
        onCanceled: root.dragIndex = -1
        onDoubleClicked: mouse => {
            const hit = root.nearest(mouse.x, mouse.y)
            if (hit >= 0) root.edited(root.points.filter((_, i) => i !== hit))
        }
    }
}
