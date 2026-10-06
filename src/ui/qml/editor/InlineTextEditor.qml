import QtQuick
import QtQuick.Controls.Basic

// Edits a text layer in place on the canvas, at about the size it is drawn.
// Enter (or clicking elsewhere) keeps the change, ⇧Enter starts a new line,
// Esc cancels.
Item {
    id: root

    property ProjectController project
    property real minWidth: 120
    property real canvasHeight: 360
    signal finished(bool accepted, string text)

    visible: false
    width: Math.max(root.minWidth + 16, area.implicitWidth + 8)
    height: area.implicitHeight

    function open(text) {
        area.text = text
        root.visible = true
        area.forceActiveFocus()
        area.selectAll()
    }

    function close(accepted) {
        if (!root.visible) return
        root.visible = false
        root.finished(accepted, area.text)
    }

    TextArea {
        id: area
        anchors.fill: parent
        wrapMode: TextEdit.NoWrap
        color: "white"
        selectionColor: Theme.accent
        selectedTextColor: "white"
        padding: 6
        font.pixelSize: Math.max(11, (root.project.selection.textSize || 64) * root.canvasHeight / 1080)
        font.weight: (root.project.selection.textWeight || 600) >= 600 ? Font.DemiBold : Font.Normal
        horizontalAlignment: root.project.selection.textAlignment === "left" ? TextEdit.AlignLeft
                           : root.project.selection.textAlignment === "right" ? TextEdit.AlignRight
                           : TextEdit.AlignHCenter
        background: Rectangle {
            color: Qt.rgba(0.05, 0.06, 0.08, 0.9)
            radius: 4
            border.width: 1.5
            border.color: Theme.accent
        }
        Keys.onReturnPressed: event => {
            if (event.modifiers & Qt.ShiftModifier) event.accepted = false
            else root.close(true)
        }
        Keys.onEnterPressed: event => {
            if (event.modifiers & Qt.ShiftModifier) event.accepted = false
            else root.close(true)
        }
        Keys.onEscapePressed: root.close(false)
        onActiveFocusChanged: if (!activeFocus) root.close(true)
    }
}
