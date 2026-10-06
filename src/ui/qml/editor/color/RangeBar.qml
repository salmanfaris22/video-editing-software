import QtQuick
import QtQuick.Controls.Basic

// A qualifier range on a gradient bar (Resolve's Qualifier palette): the
// selected range is bright with soft shoulders on both sides; drag a handle
// to move one end, drag inside to move the whole range. With `wrap` (hue)
// the range may cross the ends of the bar.
Item {
    id: root

    property real low: 0.2
    property real high: 0.8
    property real soft: 0.05
    property bool wrap: false
    property var stops: [{ at: 0, color: "#000000" }, { at: 1, color: "#FFFFFF" }]
    property bool active: true
    signal edited(real low, real high)

    implicitHeight: 26
    implicitWidth: 240

    readonly property real span: wrap ? Math.max(0, Math.min(1, high - low)) : Math.max(0, high - low)
    function norm(v) { return root.wrap ? ((v % 1) + 1) % 1 : Math.max(0, Math.min(1, v)) }

    Item {  // the gradient track (geometry for the canvases and handles)
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 12
    }
    Canvas {
        id: trackPaint
        anchors.fill: track
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const g = ctx.createLinearGradient(0, 0, width, 0)
            for (const s of root.stops) g.addColorStop(s.at, s.color)
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = String(Theme.strokeStrong)
            ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
        }
        onWidthChanged: requestPaint()
        Connections {
            target: root
            function onStopsChanged() { trackPaint.requestPaint() }
        }
        Connections {
            target: Theme
            function onModeChanged() { trackPaint.requestPaint() }
        }
    }
    // Dim what is not selected (drawn as one or two dark bands).
    Canvas {
        id: shade
        anchors.fill: track
        opacity: root.active ? 1 : 0.4
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const w = width, h = height
            let lo = root.low, hi = root.high
            if (root.wrap) { lo = root.norm(lo); hi = root.norm(hi) }
            ctx.fillStyle = "rgba(10,11,14,0.72)"
            const crosses = root.wrap && lo > hi
            if (!crosses) {
                ctx.fillRect(0, 0, Math.max(0, lo * w), h)
                ctx.fillRect(Math.min(w, hi * w), 0, w - hi * w, h)
            } else {
                ctx.fillRect(hi * w, 0, Math.max(0, (lo - hi) * w), h)
            }
            ctx.strokeStyle = "rgba(255,255,255,0.85)"
            ctx.lineWidth = 1
            for (const x of [lo * w, hi * w]) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke() }
            ctx.strokeStyle = "rgba(255,255,255,0.35)"
            ctx.setLineDash([2, 2])
            for (const x of [(lo - root.soft) * w, (hi + root.soft) * w]) {
                const xx = root.wrap ? ((x % w) + w) % w : x
                if (xx < 0 || xx > w) continue
                ctx.beginPath(); ctx.moveTo(xx, 0); ctx.lineTo(xx, h); ctx.stroke()
            }
        }
        Connections {
            target: root
            function onLowChanged() { shade.requestPaint() }
            function onHighChanged() { shade.requestPaint() }
            function onSoftChanged() { shade.requestPaint() }
            function onActiveChanged() { shade.requestPaint() }
        }
        onWidthChanged: requestPaint()
    }
    Repeater {  // end handles
        model: 2
        delegate: Rectangle {
            required property int index
            readonly property real v: root.norm(index === 0 ? root.low : root.high)
            x: v * track.width - width / 2
            y: track.y - 3
            width: 7
            height: track.height + 6
            radius: 2
            color: dragArea.pressed && dragArea.which === index ? Theme.accent : Theme.knob
            border.color: "#0B0C10"
            visible: root.active
        }
    }
    MouseArea {
        id: dragArea
        anchors.fill: parent
        enabled: root.active
        preventStealing: true
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        property int which: -1  // 0 low, 1 high, 2 whole range
        property real pressAt: 0
        property real startLow: 0
        property real startHigh: 0
        onPressed: mouse => {
            const v = mouse.x / width
            const lo = root.norm(root.low), hi = root.norm(root.high)
            const dLo = Math.abs(v - lo), dHi = Math.abs(v - hi)
            const near = 8 / width
            which = Math.min(dLo, dHi) < near ? (dLo <= dHi ? 0 : 1) : 2
            pressAt = v
            startLow = root.low
            startHigh = root.high
        }
        onPositionChanged: mouse => {
            if (!pressed) return
            const fine = mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1
            const d = (mouse.x / width - pressAt) * fine
            let lo = startLow, hi = startHigh
            if (which === 0) lo = startLow + d
            else if (which === 1) hi = startHigh + d
            else { lo = startLow + d; hi = startHigh + d }
            if (!root.wrap) {
                if (which === 2) {
                    const width0 = startHigh - startLow
                    lo = Math.max(0, Math.min(1 - width0, lo))
                    hi = lo + width0
                } else {
                    lo = Math.max(0, Math.min(lo, hi - 0.01))
                    hi = Math.min(1, Math.max(hi, lo + 0.01))
                }
            } else {
                if (which === 0) lo = Math.min(lo, hi - 0.005)
                if (which === 1) hi = Math.max(hi, lo + 0.005)
                if (hi - lo > 1) hi = lo + 1
            }
            root.edited(lo, hi)
        }
        onReleased: which = -1
    }
}
