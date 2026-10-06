import QtQuick
import QtQuick.Controls.Basic

// One primary color wheel (Lift, Gamma, Gain or Offset), drawn like DaVinci
// Resolve: a hue ring aligned with the vectorscope, a shaded disc with ticks,
// a puck on a line from the center, Y R G B readouts and a ribbed master
// jog wheel. Drag the puck (⇧ = fine), drag the jog for luminance,
// double-click either to reset it.
Item {
    id: root

    property ProjectController project
    property string wheel: "lift"
    property string title: "Lift"
    property var value: ({ x: 0, y: 0, master: 0 })
    property real dialSize: 140
    signal edited(real x, real y, real master)

    readonly property real vx: root.value.x || 0
    readonly property real vy: root.value.y || 0
    readonly property real vm: root.value.master || 0
    readonly property var channels: root.project ? root.project.wheelChannels(root.wheel, vx, vy, vm) : [0, 0, 0, 0]
    readonly property bool changed: vx !== 0 || vy !== 0 || vm !== 0

    implicitWidth: dialSize + 20
    implicitHeight: header.height + dial.height + readouts.height + jog.height + 24

    // Title and reset
    Item {
        id: header
        width: parent.width
        height: 18
        Label {
            anchors.centerIn: parent
            text: root.title
            color: root.changed ? Theme.text : Theme.textSecondary
            font.pixelSize: 12
            font.weight: root.changed ? Font.DemiBold : Font.Normal
        }
        AbstractButton {
            objectName: "reset-" + root.wheel
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            opacity: root.changed ? 1 : 0.45
            onClicked: root.edited(0, 0, 0)
            ToolTip.visible: hovered
            ToolTip.text: "Reset " + root.title
            ToolTip.delay: 500
            contentItem: Icon { name: "undo"; size: 11; color: parent.hovered ? Theme.text : Theme.textMuted }
            background: Rectangle { radius: 9; color: parent.hovered ? Theme.hover : "transparent" }
        }
    }

    Item {
        id: dial
        objectName: "wheel-" + root.wheel
        anchors.top: header.bottom
        anchors.topMargin: 6
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.dialSize
        height: root.dialSize
        readonly property real radius: width / 2
        readonly property real ring: Math.max(7, radius * 0.12)
        readonly property real reach: radius - ring - 6  // puck travel

        // Ring, disc, ticks and crosshair (static; repainted on resize).
        Canvas {
            id: face
            anchors.fill: parent
            renderStrategy: Canvas.Cooperative
            function colorAt(t) {  // the color a push toward angle t gives (BT.709 Cb/Cr)
                const cb = Math.cos(t) * 0.32, cr = Math.sin(t) * 0.32
                return Qt.rgba(Math.max(0, Math.min(1, 0.5 + 1.5748 * cr)),
                               Math.max(0, Math.min(1, 0.5 - 0.1873 * cb - 0.4681 * cr)),
                               Math.max(0, Math.min(1, 0.5 + 1.8556 * cb)), 1)
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2, cy = height / 2, r = dial.radius, w = dial.ring
                // Hue ring: a conical gradient (counter-clockwise from 3 o'clock = vectorscope angles).
                const g = ctx.createConicalGradient(cx, cy, 0)
                for (let i = 0; i <= 36; ++i) g.addColorStop(i / 36, colorAt(i / 36 * Math.PI * 2))
                ctx.beginPath()
                ctx.arc(cx, cy, r - 1, 0, Math.PI * 2)
                ctx.arc(cx, cy, r - w, 0, Math.PI * 2, true)
                ctx.fillStyle = g
                ctx.fill()
                // Thin dark separators on both sides of the ring.
                ctx.lineWidth = 1
                ctx.strokeStyle = "rgba(0,0,0,0.65)"
                ctx.beginPath(); ctx.arc(cx, cy, r - 1, 0, Math.PI * 2); ctx.stroke()
                ctx.beginPath(); ctx.arc(cx, cy, r - w, 0, Math.PI * 2); ctx.stroke()
                // Inner disc: lighter at the center like a lit dome.
                const disc = ctx.createRadialGradient(cx - r * 0.12, cy - r * 0.15, 0, cx, cy, r - w - 1)
                disc.addColorStop(0, "#30333C")
                disc.addColorStop(0.75, "#1D1F25")
                disc.addColorStop(1, "#14161A")
                ctx.beginPath()
                ctx.arc(cx, cy, r - w - 1, 0, Math.PI * 2)
                ctx.fillStyle = disc
                ctx.fill()
                // Ticks every 15°, longer every 45°.
                for (let a = 0; a < 360; a += 15) {
                    const t = a * Math.PI / 180
                    const len = a % 45 === 0 ? 6 : 3
                    const r0 = r - w - 3, r1 = r0 - len
                    ctx.beginPath()
                    ctx.strokeStyle = a % 45 === 0 ? "rgba(255,255,255,0.22)" : "rgba(255,255,255,0.10)"
                    ctx.moveTo(cx + Math.cos(t) * r0, cy - Math.sin(t) * r0)
                    ctx.lineTo(cx + Math.cos(t) * r1, cy - Math.sin(t) * r1)
                    ctx.stroke()
                }
                // Crosshair and the reach circle at half travel.
                ctx.strokeStyle = "rgba(255,255,255,0.07)"
                ctx.beginPath(); ctx.moveTo(cx - dial.reach, cy); ctx.lineTo(cx + dial.reach, cy); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(cx, cy - dial.reach); ctx.lineTo(cx, cy + dial.reach); ctx.stroke()
                ctx.beginPath(); ctx.arc(cx, cy, dial.reach / 2, 0, Math.PI * 2); ctx.stroke()
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        // Line from the center to the puck.
        Rectangle {
            readonly property real dx: root.vx * dial.reach
            readonly property real dy: -root.vy * dial.reach
            readonly property real len: Math.hypot(dx, dy)
            visible: len > 1
            x: dial.width / 2
            y: dial.height / 2 - height / 2
            width: len
            height: 1.5
            color: Qt.rgba(1, 1, 1, 0.55)
            transformOrigin: Item.Left
            rotation: Math.atan2(dy, dx) * 180 / Math.PI
        }
        Rectangle {  // center
            x: dial.width / 2 - 2
            y: dial.height / 2 - 2
            width: 4
            height: 4
            radius: 2
            color: Qt.rgba(1, 1, 1, 0.35)
        }
        Rectangle {  // puck
            objectName: "puck-" + root.wheel
            x: dial.width / 2 + root.vx * dial.reach - width / 2
            y: dial.height / 2 - root.vy * dial.reach - height / 2
            width: 11
            height: 11
            radius: 5.5
            color: Theme.knob
            border.color: "#0B0C10"
            border.width: 2
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor
            property point pressAt
            property real startX: 0
            property real startY: 0
            onPressed: mouse => {
                pressAt = Qt.point(mouse.x, mouse.y)
                startX = root.vx
                startY = root.vy
            }
            onPositionChanged: mouse => {
                if (!pressed) return
                // Relative drag like Resolve: the puck follows the hand from where it was (⇧ = fine).
                const k = (mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1) / dial.reach
                let x = startX + (mouse.x - pressAt.x) * k
                let y = startY - (mouse.y - pressAt.y) * k
                const d = Math.hypot(x, y)
                if (d > 1) { x /= d; y /= d }
                root.edited(x, y, root.vm)
            }
            onDoubleClicked: root.edited(0, 0, root.vm)
        }
    }

    // Y R G B readouts
    Row {
        id: readouts
        anchors.top: dial.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 3
        Repeater {
            model: ["#E6E8EE", "#F05252", "#3FCB6A", "#4D8DFF"]
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: Math.max(34, Math.min(46, (root.dialSize - 9) / 4))
                height: 18
                radius: 2
                color: Theme.inset
                border.color: Theme.stroke
                Label {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -1
                    text: Number(root.channels[index] || 0).toFixed(2)
                    color: Theme.textSecondary
                    font.pixelSize: 10
                    font.family: Theme.monoFamily
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 3
                    anchors.rightMargin: 3
                    anchors.bottomMargin: 2
                    height: 1.5
                    color: parent.modelData
                }
            }
        }
    }

    // Master jog wheel: ribs move with the value; drag left/right.
    Rectangle {
        id: jog
        objectName: "master-" + root.wheel
        anchors.top: readouts.bottom
        anchors.topMargin: 7
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(root.width - 8, root.dialSize + 30)
        height: 14
        radius: 7
        clip: true
        gradient: Gradient {
            GradientStop { position: 0; color: jogArea.pressed ? "#2C3039" : "#23262D" }
            GradientStop { position: 0.5; color: jogArea.pressed ? "#1F2229" : "#17191E" }
            GradientStop { position: 1; color: "#101216" }
        }
        border.color: "#2A2E37"
        Repeater {
            model: Math.ceil(jog.width / 5) + 2
            delegate: Rectangle {
                required property int index
                // Ribs scroll with the master value; brighter near the middle like a real wheel.
                readonly property real pos: index * 5 + ((root.vm * 60) % 5 + 5) % 5 - 5
                x: pos
                y: 3
                width: 1
                height: jog.height - 6
                color: "#FFFFFF"
                opacity: 0.08 + 0.22 * Math.max(0, 1 - Math.abs(pos - jog.width / 2) / (jog.width / 2))
            }
        }
        Rectangle {  // the master value marker
            width: 2
            height: jog.height - 4
            y: 2
            radius: 1
            color: Theme.accent
            x: (jog.width - width) * (root.vm + 1) / 2
        }
        MouseArea {
            id: jogArea
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            property real pressX: 0
            property real start: 0
            onPressed: mouse => { pressX = mouse.x; start = root.vm }
            onPositionChanged: mouse => {
                if (!pressed) return
                const fine = mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1
                root.edited(root.vx, root.vy, Math.max(-1, Math.min(1, start + (mouse.x - pressX) / 150 * fine)))
            }
            onDoubleClicked: root.edited(root.vx, root.vy, 0)
        }
    }
}
