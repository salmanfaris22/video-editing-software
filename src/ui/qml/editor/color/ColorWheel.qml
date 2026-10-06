import QtQuick
import QtQuick.Controls.Basic

// One primary color wheel (Lift, Gamma, Gain or Offset), DaVinci style:
// hue ring aligned with the vectorscope, a puck to push the color, the
// master jog strip for luminance, Y R G B readouts and a reset button.
Column {
    id: root

    property ProjectController project
    property string wheel: "lift"
    property string title: "Lift"
    property var value: ({ x: 0, y: 0, master: 0 })
    signal edited(real x, real y, real master)
    property real dialSize: Math.min(width, 150)

    spacing: 6
    readonly property var channels: root.project ? root.project.wheelChannels(root.wheel, root.value.x || 0, root.value.y || 0, root.value.master || 0) : [0, 0, 0, 0]

    Item {
        width: parent.width
        height: 20
        Label {
            anchors.centerIn: parent
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontM
        }
        IconButton {
            objectName: "reset-" + root.wheel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 22
            implicitHeight: 22
            iconName: "undo"
            iconSize: 12
            iconColor: Theme.textMuted
            tooltip: "Reset " + root.title
            onClicked: root.edited(0, 0, 0)
        }
    }

    Item {
        id: dial
        objectName: "wheel-" + root.wheel
        width: parent.width
        height: root.dialSize
        readonly property real radius: Math.min(width, height) / 2 - 4
        readonly property point center: Qt.point(width / 2, height / 2)

        Canvas {
            id: ring
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2, cy = height / 2, r = dial.radius
                // Hue ring: each angle shows the color a push in that Cb/Cr direction gives.
                for (let a = 0; a < 360; a += 2) {
                    const t = a * Math.PI / 180
                    const cb = Math.cos(t) * 0.3, cr = Math.sin(t) * 0.3
                    const R = Math.max(0, Math.min(1, 0.5 + 1.5748 * cr))
                    const G = Math.max(0, Math.min(1, 0.5 - 0.1873 * cb - 0.4681 * cr))
                    const B = Math.max(0, Math.min(1, 0.5 + 1.8556 * cb))
                    ctx.beginPath()
                    ctx.strokeStyle = Qt.rgba(R, G, B, 1)
                    ctx.lineWidth = 7
                    ctx.arc(cx, cy, r - 4, -t - 0.02, -t + 0.04 + 0.02, false)
                    ctx.stroke()
                }
                ctx.beginPath()
                ctx.fillStyle = "#15171D"
                ctx.arc(cx, cy, r - 8, 0, Math.PI * 2)
                ctx.fill()
                ctx.strokeStyle = "rgba(255,255,255,0.12)"
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(cx - r + 10, cy); ctx.lineTo(cx + r - 10, cy); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(cx, cy - r + 10); ctx.lineTo(cx, cy + r - 10); ctx.stroke()
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }
        Rectangle {  // puck
            readonly property real px: dial.center.x + (root.value.x || 0) * (dial.radius - 10)
            readonly property real py: dial.center.y - (root.value.y || 0) * (dial.radius - 10)
            x: px - width / 2
            y: py - height / 2
            width: 12
            height: 12
            radius: 6
            color: "transparent"
            border.color: "white"
            border.width: 2
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor
            function push(mouse) {
                const r = dial.radius - 10
                let x = (mouse.x - dial.center.x) / r
                let y = -(mouse.y - dial.center.y) / r
                const d = Math.hypot(x, y)
                if (d > 1) { x /= d; y /= d }
                const fine = mouse.modifiers & Qt.ShiftModifier ? 0.25 : 1
                root.edited(x * fine, y * fine, root.value.master || 0)
            }
            onPressed: mouse => push(mouse)
            onPositionChanged: mouse => { if (pressed) push(mouse) }
            onDoubleClicked: root.edited(0, 0, root.value.master || 0)
        }
    }

    Row {  // Y R G B readouts
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 4
        Repeater {
            model: [Theme.text, "#FF5A5A", "#4ADE80", "#60A5FA"]
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: 40
                height: 20
                radius: 2
                color: Theme.raised
                border.color: Theme.stroke
                Label {
                    anchors.centerIn: parent
                    text: Number(root.channels[index] || 0).toFixed(2)
                    color: Theme.text
                    font.pixelSize: 10
                    font.family: Theme.monoFamily
                }
                Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 3; height: 2; color: parent.modelData }
            }
        }
    }

    Rectangle {  // master jog: drag left/right for luminance
        objectName: "master-" + root.wheel
        width: parent.width - 16
        anchors.horizontalCenter: parent.horizontalCenter
        height: 16
        radius: 8
        color: jog.pressed ? Theme.pressed : Theme.raised
        border.color: Theme.stroke
        Row {
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: 18
                delegate: Rectangle { width: 1; height: 8; color: Theme.textFaint; opacity: 0.7 }
            }
        }
        Rectangle {  // position of the master value
            width: 3
            height: parent.height - 4
            y: 2
            radius: 1
            color: Theme.accent
            x: (parent.width - width) * ((root.value.master || 0) + 1) / 2
        }
        MouseArea {
            id: jog
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            property real pressX: 0
            property real start: 0
            onPressed: mouse => { pressX = mouse.x; start = root.value.master || 0 }
            onPositionChanged: mouse => {
                if (!pressed) return
                const fine = mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1
                const m = Math.max(-1, Math.min(1, start + (mouse.x - pressX) / 150 * fine))
                root.edited(root.value.x || 0, root.value.y || 0, m)
            }
            onDoubleClicked: root.edited(root.value.x || 0, root.value.y || 0, 0)
        }
    }
}
