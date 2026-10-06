import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Track label and switches: mute/solo for sound, hide for picture, lock.
// Click selects the layer; double-click renames it; right-click (or ⋯) for
// rename, move up/down (stacking order) and delete.
Rectangle {
    id: root

    property var track
    property ProjectController project
    readonly property bool audio: root.track.kind === "audio"
    readonly property bool selected: root.project.selectedTrack === root.track.id
    property bool renaming: false
    signal activated()
    objectName: "trackHeader-" + root.track.id

    function beginRename() {
        root.renaming = true
        nameField.text = root.track.name
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    function endRename(accept) {
        if (!root.renaming) return
        root.renaming = false
        if (accept && nameField.text.trim().length > 0 && nameField.text !== root.track.name)
            root.project.renameTrack(root.track.id, nameField.text)
        root.activated()
    }

    color: root.selected ? Theme.raised : headerHover.hovered ? Theme.hover : Theme.surface
    HoverHandler { id: headerHover }
    Rectangle {  // selected layer marker
        width: 3
        height: parent.height
        color: Theme.accent
        visible: root.selected
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: mouse => {
            root.activated()
            root.project.selectTrack(root.track.id)
            if (mouse.button === Qt.RightButton) layerMenu.popup()
        }
        onDoubleClicked: mouse => { if (mouse.button === Qt.LeftButton) root.beginRename() }
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }

    ToolTip.visible: headerHover.hovered && !root.renaming
    ToolTip.text: root.track.name + " (" + root.track.label + ")"
    ToolTip.delay: 500

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        spacing: 2
        Rectangle {
            width: 28
            height: 20
            radius: 0
            color: root.audio || root.track.kind === "subtitle" ? Theme.warningSoft : Theme.accentSoft
            Label {
                anchors.centerIn: parent
                text: root.track.label
                color: root.audio ? Theme.warning : root.track.kind === "subtitle" ? "#E8C547" : Theme.accent
                font.pixelSize: 10
                font.weight: Font.Bold
            }
        }
        Item { Layout.fillWidth: true; Layout.minimumWidth: 4 }
        TextField {
            id: nameField
            objectName: "layerNameField"
            visible: root.renaming
            Layout.fillWidth: true
            Layout.leftMargin: 2
            implicitHeight: 24
            font.pixelSize: Theme.fontS
            color: Theme.text
            selectByMouse: true
            background: Rectangle { color: Theme.bg; border.color: Theme.accent; border.width: 1 }
            onAccepted: root.endRename(true)
            onActiveFocusChanged: if (!activeFocus) root.endRename(true)
            Keys.onEscapePressed: root.endRename(false)
        }
        IconButton {
            visible: root.track.hasAudio
            implicitWidth: 24
            implicitHeight: 24
            iconSize: 13
            iconName: root.track.muted ? "speaker-off" : "speaker"
            iconColor: root.track.muted ? Theme.danger : Theme.textMuted
            tooltip: root.track.muted ? "Unmute" : "Mute"
            onClicked: root.project.setTrackValue(root.track.id, "muted", !root.track.muted)
        }
        AbstractButton {
            visible: root.track.hasAudio
            implicitWidth: 22
            implicitHeight: 24
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            onClicked: root.project.setTrackValue(root.track.id, "solo", !root.track.solo)
            ToolTip.visible: hovered
            ToolTip.text: "Solo"
            background: Rectangle { radius: 0; color: root.track.solo ? Theme.warningSoft : parent.hovered ? Theme.hover : "transparent" }
            contentItem: Label {
                text: "S"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: root.track.solo ? Theme.warning : Theme.textFaint
                font.pixelSize: 11
                font.weight: Font.Bold
            }
        }
        IconButton {
            visible: !root.audio
            implicitWidth: 24
            implicitHeight: 24
            iconSize: 13
            iconName: root.track.hidden ? "eye-off" : "eye"
            iconColor: root.track.hidden ? Theme.danger : Theme.textMuted
            tooltip: root.track.hidden ? "Show track" : "Hide track"
            onClicked: root.project.setTrackValue(root.track.id, "hidden", !root.track.hidden)
        }
        IconButton {
            implicitWidth: 24
            implicitHeight: 24
            iconSize: 13
            iconName: root.track.locked ? "lock" : "unlock"
            iconColor: root.track.locked ? Theme.warning : Theme.textFaint
            tooltip: root.track.locked ? "Unlock (edits and ripple deletes skip locked tracks)" : "Lock"
            onClicked: root.project.setTrackValue(root.track.id, "locked", !root.track.locked)
        }
    }

    LecternMenu {
        id: layerMenu
        LecternMenuItem { text: "Rename…"; onTriggered: root.beginRename() }
        LecternMenuItem {
            text: "Move layer up"
            enabled: root.track.canMoveUp
            onTriggered: root.project.moveTrack(root.track.id, 1)
        }
        LecternMenuItem {
            text: "Move layer down"
            enabled: root.track.canMoveDown
            onTriggered: root.project.moveTrack(root.track.id, -1)
        }
        MenuSeparator {}
        LecternMenuItem {
            text: root.track.clips.length > 0 ? "Delete layer and its " + root.track.clips.length
                                                + (root.track.clips.length === 1 ? " clip" : " clips")
                                              : "Delete layer"
            enabled: !root.track.locked
            onTriggered: root.project.deleteTrack(root.track.id)
        }
    }
}
