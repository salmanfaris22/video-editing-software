import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The editor: tool rail + panel, the live preview with transport, and the
// timeline. Keyboard: Space play/pause · S split · ⌫ delete · M marker ·
// ←/→ frame (⇧ = 1 s) · Home/End · I/O mark a section · Esc deselect ·
// ⌘A select all · drag on timeline to box-select (mode in toolbar) · ⌘Z/⌘⇧Z · ⌘E export.
Item {
    id: root

    required property AppController app
    readonly property ProjectController project: app.project
    readonly property PlaybackController playback: app.playback
    property string tool: root.app.workspaceValue("editorTool", "layout")
    onToolChanged: root.app.setWorkspaceValue("editorTool", root.tool)
    property real markIn: -1
    property real markOut: -1
    /// Timeline empty-lane gesture: drag = box-select · shift = ⇧-drag only · scrub = click seeks only
    property string dragSelectMode: root.app.workspaceValue("timelineDragSelect", "drag")
    onDragSelectModeChanged: root.app.setWorkspaceValue("timelineDragSelect", dragSelectMode)
    /// Clips included in the box: touch = overlaps · inside = fully inside the box
    property string marqueeMatch: root.app.workspaceValue("timelineMarqueeMatch", "touch")
    onMarqueeMatchChanged: root.app.setWorkspaceValue("timelineMarqueeMatch", marqueeMatch)
    /// track = one clip / gap delete · allTracks = linked recording on every track
    property string linkedEditMode: root.app.workspaceValue("timelineLinkedEdit", "track")
    onLinkedEditModeChanged: {
        root.app.setWorkspaceValue("timelineLinkedEdit", linkedEditMode)
        root.project.linkedEditMode = linkedEditMode
    }
    property real timelineTrackHeight: root.app.workspaceValue("timelineTrackHeight", 48)
    onTimelineTrackHeightChanged: root.app.setWorkspaceValue("timelineTrackHeight", timelineTrackHeight)
    /// Page (Resolve-style page bar at the bottom): "edit" or "color".
    property string page: root.app.workspaceValue("editorPage", "edit")
    onPageChanged: {
        root.app.setWorkspaceValue("editorPage", page)
        console.info("page:", page)
    }
    property string scopeMode: root.app.workspaceValue("scopeMode", "parade")
    property string colorPalette: root.app.workspaceValue("colorPalette", "primaries")
    onColorPaletteChanged: root.app.setWorkspaceValue("colorPalette", colorPalette)
    onScopeModeChanged: root.app.setWorkspaceValue("scopeMode", scopeMode)
    property string timelineGridMode: root.app.workspaceValue("timelineGridMode", "auto")
    onTimelineGridModeChanged: root.app.setWorkspaceValue("timelineGridMode", timelineGridMode)

    function refocus() { root.forceActiveFocus() }
    function openExport() { exportDialog.open() }

    Component.onCompleted: root.project.linkedEditMode = linkedEditMode

    focus: true
    Keys.onPressed: event => {
        const shift = event.modifiers & Qt.ShiftModifier
        const command = event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)
        if (command) return
        switch (event.key) {
        case Qt.Key_Space: root.playback.toggle(); break
        case Qt.Key_S: root.project.splitAt(root.playback.position); break
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            if (shift && root.project.selection.start !== undefined) {  // ripple delete: close the gap
                const sel = root.project.selection
                root.project.removeRange(sel.start, sel.start + sel.duration)
            } else {
                root.project.deleteSelected()
            }
            break
        case Qt.Key_D:  // enable / disable the selected clip (Resolve)
            if (root.project.selectedClip.length > 0)
                root.project.setClipEnabled(root.project.selectedClip, root.project.selection.enabled === false)
            break
        case Qt.Key_M: root.project.addMarker(root.playback.position, ""); break
        case Qt.Key_Left: shift ? root.playback.seek(root.playback.position - 1) : root.playback.step(-1); break
        case Qt.Key_Right: shift ? root.playback.seek(root.playback.position + 1) : root.playback.step(1); break
        case Qt.Key_Home: root.playback.seek(0); break
        case Qt.Key_End: root.playback.seek(root.project.duration); break
        case Qt.Key_I: root.markIn = root.playback.position; if (root.markOut <= root.markIn) root.markOut = -1; break
        case Qt.Key_O: root.markOut = root.playback.position; if (root.markIn < 0 || root.markIn >= root.markOut) root.markIn = 0; break
        case Qt.Key_Escape: root.project.clearSelection(); root.markIn = -1; root.markOut = -1; break
        default: return
        }
        event.accepted = true
    }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.WindowShortcut; enabled: root.project.canUndo; onActivated: root.project.undo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.WindowShortcut; enabled: root.project.canRedo; onActivated: root.project.redo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; context: Qt.WindowShortcut; enabled: root.project.canRedo; onActivated: root.project.redo() }
    Shortcut { sequence: "Ctrl+E"; context: Qt.WindowShortcut; enabled: root.project.duration > 0; onActivated: exportDialog.open() }
    Shortcut { sequence: "Ctrl+S"; context: Qt.WindowShortcut; onActivated: root.project.save() }
    Shortcut { sequences: [StandardKey.SelectAll]; context: Qt.WindowShortcut; enabled: root.project.duration > 0; onActivated: root.project.selectAllClips() }
    Shortcut { sequence: "Space"; context: Qt.WindowShortcut; onActivated: root.playback.toggle() }
    Shortcut { sequence: "S"; context: Qt.WindowShortcut; enabled: root.project.duration > 0; onActivated: root.project.splitAt(root.playback.position) }
    Shortcut { sequence: "Delete"; context: Qt.WindowShortcut; enabled: root.project.selectedClips.length > 0; onActivated: root.project.deleteSelected() }
    Shortcut { sequence: "Backspace"; context: Qt.WindowShortcut; enabled: root.project.selectedClips.length > 0; onActivated: root.project.deleteSelected() }
    Shortcut {  // ripple delete: remove the selected clip's time on every track
        sequences: ["Shift+Backspace", "Shift+Delete"]
        context: Qt.WindowShortcut
        enabled: root.project.selection.start !== undefined
        onActivated: root.project.removeRange(root.project.selection.start, root.project.selection.start + root.project.selection.duration)
    }
    Shortcut {  // enable / disable the selected clip, like Resolve
        sequence: "D"
        context: Qt.WindowShortcut
        enabled: root.project.selectedClip.length > 0
        onActivated: root.project.setClipEnabled(root.project.selectedClip, root.project.selection.enabled === false)
    }
    Shortcut { sequence: "M"; context: Qt.WindowShortcut; enabled: root.project.duration > 0; onActivated: root.project.addMarker(root.playback.position, "") }
    Shortcut { sequence: "Escape"; context: Qt.WindowShortcut; onActivated: { root.project.clearSelection(); root.markIn = -1; root.markOut = -1 } }

    EditorTopBar {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        app: root.app
        onExportRequested: exportDialog.open()
    }

    Shortcut { sequence: "Shift+4"; context: Qt.WindowShortcut; onActivated: root.page = "edit" }
    Shortcut { sequence: "Shift+6"; context: Qt.WindowShortcut; onActivated: root.page = "color" }

    ColorPage {
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: pageBar.top
        visible: root.page === "color"
        project: root.project
        playback: root.playback
        editor: root
        onActivated: root.refocus()
    }

    // ---- Page bar (bottom), like DaVinci Resolve ------------------------------
    Rectangle {
        id: pageBar
        objectName: "pageBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 44
        color: Theme.surface
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Theme.stroke }
        Row {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Image {
                width: 22
                height: 22
                anchors.verticalCenter: parent.verticalCenter
                source: "qrc:/qt/qml/Lectern/UI/brand/lectern-icon-256.png"
                sourceSize: Qt.size(44, 44)
                mipmap: true
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "Lectern"
                color: Theme.text
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
            }
        }
        Row {
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: [
                    { id: "edit", label: "Edit", icon: "cut", keys: "⇧4" },
                    { id: "color", label: "Color", icon: "adjust", keys: "⇧6" }
                ]
                delegate: AbstractButton {
                    id: pageButton
                    required property var modelData
                    objectName: "page-" + modelData.id
                    readonly property bool current: root.page === modelData.id
                    width: 120
                    height: pageBar.height
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: { root.page = modelData.id; root.refocus() }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.label + " page (" + modelData.keys + ")"
                    ToolTip.delay: 500
                    background: Rectangle {
                        color: pageButton.current ? "#0B0C10" : pageButton.hovered ? Theme.hover : "transparent"
                        Rectangle {  // the current page, underlined in red like Resolve
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            color: Theme.record
                            visible: pageButton.current
                        }
                    }
                    contentItem: Row {
                        spacing: 6
                        leftPadding: (pageButton.width - implicitWidth) / 2
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: pageButton.modelData.icon
                            size: 16
                            color: pageButton.current ? Theme.text : Theme.textMuted
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: pageButton.modelData.label
                            color: pageButton.current ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fontS
                            font.weight: pageButton.current ? Font.DemiBold : Font.Normal
                        }
                    }
                }
            }
        }
    }

    SplitView {
        id: split
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: pageBar.top
        visible: root.page === "edit"
        orientation: Qt.Vertical

        handle: Rectangle {
            implicitHeight: 5
            color: SplitHandle.pressed ? Theme.accent : SplitHandle.hovered ? Theme.strokeStrong : Theme.stroke
        }

        Item {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 280
            RowLayout {
                anchors.fill: parent
                spacing: 0
                ToolRail {
                    Layout.fillHeight: true
                    tool: root.tool
                    onToolSelected: t => { root.tool = t; root.refocus() }
                }
                ToolPanel {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 312
                    tool: root.tool
                    project: root.project
                    playback: root.playback
                    editor: root
                }
                PreviewPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    project: root.project
                    playback: root.playback
                    onActivated: root.refocus()
                }
            }
        }

        TimelinePanel {
            SplitView.preferredHeight: 300
            SplitView.minimumHeight: 170
            project: root.project
            playback: root.playback
            editor: root
            onActivated: root.refocus()
        }

        Component.onCompleted: {
            const state = root.app.workspaceValue("editorSplit")
            if (state) split.restoreState(state)
        }
        Component.onDestruction: root.app.setWorkspaceValue("editorSplit", split.saveState())
    }

    // Transient messages from the controller ("Nothing to split here", import results, …).
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: topBar.bottom
        anchors.topMargin: 12
        width: Math.min(560, toastLabel.implicitWidth + 36)
        height: 38
        radius: 19
        color: Theme.raised
        border.width: 1
        border.color: Theme.strokeStrong
        opacity: root.project.message.length > 0 ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Label {
            id: toastLabel
            anchors.centerIn: parent
            width: Math.min(implicitWidth, 524)
            text: root.project.message
            color: Theme.text
            font.pixelSize: Theme.fontS
            elide: Text.ElideRight
        }
        Timer {
            running: root.project.message.length > 0
            interval: 4500
            onTriggered: toast.opacity = 0
        }
        Connections {
            target: root.project
            function onMessageChanged() { if (root.project.message.length > 0) toast.opacity = 1 }
        }
    }

    ExportDialog {
        id: exportDialog
        app: root.app
        onClosed: root.refocus()
    }
}
