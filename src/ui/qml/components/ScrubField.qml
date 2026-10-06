import QtQuick
import QtQuick.Controls.Basic

// Resolve / After Effects style number: drag left/right to change it,
// double-click to reset, click once to type a value. ⇧ drags finer.
Item {
    id: root

    property string label
    property real value: 0
    property real from: -1
    property real to: 1
    property real defaultValue: 0
    property real dragStep: (to - from) / 400  // value per pixel
    property int decimals: 2
    property real displayScale: 1  // shown value = value × displayScale
    property color accent: Theme.textFaint
    signal edited(real value)

    implicitWidth: row.implicitWidth
    implicitHeight: 26

    function commit(v) { root.edited(Math.max(root.from, Math.min(root.to, v))) }

    Row {
        id: row
        spacing: 6
        anchors.verticalCenter: parent.verticalCenter
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: root.label
            color: Theme.textMuted
            font.pixelSize: Theme.fontS
        }
        Rectangle {
            id: box
            width: 62
            height: 22
            radius: 2
            color: field.activeFocus ? Theme.bg : scrub.containsMouse || scrub.pressed ? Theme.hover : Theme.raised
            border.color: field.activeFocus ? Theme.accent : Theme.stroke
            Rectangle {  // colored underline like Resolve
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 3
                height: 2
                radius: 1
                color: root.accent
                opacity: 0.8
            }
            TextInput {
                id: field
                anchors.fill: parent
                anchors.margins: 3
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.text
                font.pixelSize: Theme.fontS
                font.family: Theme.monoFamily
                selectByMouse: true
                validator: DoubleValidator {}
                text: (root.value * root.displayScale).toFixed(root.decimals)
                enabled: false
                onAccepted: { root.commit(Number(text) / root.displayScale); enabled = false; focus = false }
                onActiveFocusChanged: if (!activeFocus) enabled = false
                Keys.onEscapePressed: { enabled = false; focus = false }
            }
            MouseArea {
                id: scrub
                anchors.fill: parent
                visible: !field.enabled
                hoverEnabled: true
                preventStealing: true
                cursorShape: Qt.SizeHorCursor
                property real pressX: 0
                property real startValue: 0
                property bool moved: false
                property bool afterDoubleClick: false
                onPressed: mouse => { pressX = mouse.x; startValue = root.value; moved = false }
                onPositionChanged: mouse => {
                    if (!pressed) return
                    const dx = mouse.x - pressX
                    if (Math.abs(dx) > 2) moved = true
                    if (!moved) return
                    const fine = mouse.modifiers & Qt.ShiftModifier ? 0.2 : 1
                    root.commit(startValue + dx * root.dragStep * fine)
                }
                // A single click types a value — but only once it is clearly not a double-click.
                onReleased: {
                    if (afterDoubleClick) {  // the release that ends a double-click: not a click
                        afterDoubleClick = false
                        return
                    }
                    if (!moved) typeLater.restart()
                }
                onDoubleClicked: {
                    typeLater.stop()
                    afterDoubleClick = true
                    root.commit(root.defaultValue)
                }
            }
            Timer {
                id: typeLater
                interval: Qt.styleHints.mouseDoubleClickInterval + 20
                onTriggered: {
                    field.enabled = true
                    field.forceActiveFocus()
                    field.selectAll()
                }
            }
        }
    }
}
