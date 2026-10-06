import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Color page, laid out like DaVinci Resolve's: Gallery / Looks / LUTs on
// the left, the viewer (wipe, side by side, bypass, highlight, window
// handles, color picker), the node graph and the Color Space Transform /
// effects panel on top; the clip strip; and the palettes — Primaries wheels,
// Curves / Qualifier / Window / Magic Mask, Scopes. Every control is a
// regular undoable edit of the selected node (01 = the clip's correction).
Item {
    id: root
    objectName: "colorPage"

    property ProjectController project
    property PlaybackController playback
    property var editor
    signal activated()

    readonly property var sel: root.project.selection
    readonly property string clipId: root.project.selectedClip
    readonly property bool gradable: root.sel.visual === true && root.sel.role !== "text"

    // ---- What the palettes edit ----------------------------------------------------
    /// "" = node 01, the clip's own correction; otherwise a node id.
    property string nodeId: ""
    readonly property var nodes: root.sel.nodes || []
    readonly property var node: {
        for (const n of root.nodes) if (n.id === root.nodeId) return n
        return null
    }
    onNodesChanged: if (root.nodeId.length > 0 && !root.node) root.nodeId = ""
    onClipIdChanged: {
        root.nodeId = ""
        root.picking = false
        root.setHighlight(false)
    }
    readonly property var grade: root.node ? root.node : root.sel
    readonly property string nodeName: root.node ? ((root.nodes.findIndex(n => n.id === root.nodeId) + 2).toString().padStart(2, "0") + "  " + (root.node.label || "Node")) : "01  Correction"

    // The center palette, remembered in the workspace (read once, written on change).
    property string centerPalette: "curves"
    onCenterPaletteChanged: if (root.editor && root.editor.colorPalette !== centerPalette) root.editor.colorPalette = centerPalette
    property bool picking: false
    property bool highlight: false
    property string compare: "off"  // off · wipe · side
    property bool bypass: false

    function applyCompare() { if (root.visible) root.playback.compareMode = root.bypass ? "bypass" : root.compare }
    onCompareChanged: applyCompare()
    onBypassChanged: applyCompare()
    onVisibleChanged: {
        if (visible) {
            applyCompare()
            Qt.callLater(ensureSelection)
        } else {  // the Edit page shows the plain graded picture
            root.playback.compareMode = "off"
            root.playback.setHighlight("", "")
            root.picking = false
            root.project.cancelPreview()
        }
    }
    function setHighlight(on) {
        root.highlight = on && root.nodeId.length > 0
        root.playback.setHighlight(root.clipId, root.highlight ? root.nodeId : "")
    }
    onNodeIdChanged: {
        root.picking = false
        if (root.highlight) root.setHighlight(root.nodeId.length > 0)
    }

    function setValue(key, v) {
        if (!root.gradable) return
        if (root.node) root.project.setNodeValue(root.clipId, root.nodeId, key, v)
        else root.project.setColorValue(root.clipId, key, v)
    }
    function setWheel(name, x, y, m) {
        if (!root.gradable) return
        if (root.node) root.project.setNodeWheel(root.clipId, root.nodeId, name, x, y, m)
        else root.project.setColorWheel(root.clipId, name, x, y, m)
    }
    function setCurve(channel, pts) {
        if (!root.gradable) return
        if (root.node) root.project.setNodeCurve(root.clipId, root.nodeId, channel, pts)
        else root.project.setCurve(root.clipId, channel, pts)
    }
    function resetGrade() {
        if (root.node) root.project.resetNode(root.clipId, root.nodeId)
        else root.project.resetColor(root.clipId)
    }
    function addNode(kind) {
        const id = root.project.addNode(root.clipId, kind)
        if (id.length > 0) root.nodeId = id
        return id
    }

    /// The clips that can be graded (pictures, not text), in timeline order.
    readonly property var clips: {
        const out = []
        for (const track of root.project.tracks) {
            if (track.kind === "audio" || track.kind === "subtitle") continue
            for (const clip of track.clips) {
                if (clip.kind === "text" || clip.role === "text") continue
                out.push({ id: clip.id, name: clip.name, start: clip.start, duration: clip.duration, media: clip.media,
                           sourceIn: clip.sourceIn, label: track.label, role: clip.role })
            }
        }
        out.sort((a, b) => a.start - b.start || a.label.localeCompare(b.label))
        return out
    }
    function gotoClip(c) {
        root.project.selectClip(c.id)
        const p = root.playback.position
        if (p < c.start || p >= c.start + c.duration) root.playback.seek(c.start + Math.min(0.5, c.duration / 2))
    }
    function stepClip(delta) {
        const i = root.clips.findIndex(c => c.id === root.clipId)
        const next = root.clips[Math.max(0, Math.min(root.clips.length - 1, (i < 0 ? 0 : i + delta)))]
        if (next) root.gotoClip(next)
    }

    /// Picks the clip to grade when nothing gradable is selected: the screen (or camera) at the playhead.
    function ensureSelection() {
        if (root.gradable) return
        const t = root.playback.position
        const id = root.project.roleClipAt("screen", t) || root.project.roleClipAt("camera", t)
        if (id) root.project.selectClip(id)
        else if (root.clips.length > 0) root.project.selectClip(root.clips[0].id)
    }
    Component.onCompleted: {
        if (root.editor && ["curves", "qualifier", "window", "mask"].indexOf(root.editor.colorPalette) >= 0)
            root.centerPalette = root.editor.colorPalette
        Qt.callLater(ensureSelection)
    }

    // ---- Keyboard (Color page only), as in Resolve ---------------------------------
    Shortcut { sequence: "Ctrl+C"; enabled: root.visible && root.gradable; onActivated: root.project.copyGrade(root.clipId) }
    Shortcut {
        sequence: "Ctrl+V"
        enabled: root.visible && root.project.hasCopiedGrade
        onActivated: root.project.pasteGrade(root.project.selectedClips.length > 0 ? root.project.selectedClips : [root.clipId])
    }
    Shortcut { sequence: "="; enabled: root.visible && root.gradable; onActivated: root.project.applyPreviousGrade(root.clipId) }
    Shortcut { sequence: "Shift+D"; enabled: root.visible; onActivated: root.bypass = !root.bypass }
    Shortcut { sequence: "Ctrl+W"; enabled: root.visible; onActivated: root.compare = root.compare === "wipe" ? "off" : "wipe" }
    Shortcut { sequence: "Shift+H"; enabled: root.visible; onActivated: root.setHighlight(!root.highlight) }
    Shortcut { sequence: "Alt+S"; enabled: root.visible && root.gradable && root.nodes.length < 8; onActivated: root.addNode("") }
    Shortcut { sequence: "Ctrl+Alt+G"; enabled: root.visible && root.gradable; onActivated: root.project.grabStill(root.clipId, root.playback.position) }
    Shortcut { sequence: "Up"; enabled: root.visible; onActivated: root.stepClip(-1) }
    Shortcut { sequence: "Down"; enabled: root.visible; onActivated: root.stepClip(1) }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 5
            color: SplitHandle.pressed ? Theme.accent : SplitHandle.hovered ? Theme.strokeStrong : Theme.stroke
        }

        // ---- Top: gallery · viewer · nodes · settings ----------------------------------
        SplitView {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 220
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.pressed ? Theme.accent : SplitHandle.hovered ? Theme.strokeStrong : Theme.stroke
            }

            GalleryPanel {
                id: gallery
                SplitView.preferredWidth: 250
                SplitView.minimumWidth: 180
                project: root.project
                playback: root.playback
                clipId: root.clipId
                sel: root.sel
                tab: "looks"
                onCompareWithStill: stillId => {
                    root.playback.setReferenceStill(root.project.stillImagePath(stillId))
                    if (root.compare === "off") root.compare = "wipe"
                    root.bypass = false
                }
            }

            Item {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 320
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {  // viewer toolbar
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        color: "#121419"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                text: root.gradable ? (root.sel.name || "") : "Select a clip to grade"
                                color: Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            Label { text: root.project.formatTimecode(root.playback.position); color: Theme.textMuted; font.pixelSize: 11; font.family: Theme.monoFamily }
                            Rectangle { width: 1; height: 16; color: "#2A2E37" }
                            PaletteButton {
                                objectName: "compareWipe"
                                compact: true
                                text: "Wipe"
                                checkable: true
                                checked: root.compare === "wipe"
                                tip: "Before | after split, drag the line (⌘W)"
                                onClicked: root.compare = root.compare === "wipe" ? "off" : "wipe"
                            }
                            PaletteButton {
                                objectName: "compareSide"
                                compact: true
                                text: "Side by side"
                                checkable: true
                                checked: root.compare === "side"
                                tip: "Before and after next to each other"
                                onClicked: root.compare = root.compare === "side" ? "off" : "side"
                            }
                            PaletteButton {
                                objectName: "bypassGrades"
                                compact: true
                                text: "Bypass"
                                checkable: true
                                checked: root.bypass
                                accentColor: Theme.warning
                                tip: "Turn every grade off to see the original (⇧D)"
                                onClicked: root.bypass = !root.bypass
                            }
                            PaletteButton {
                                compact: true
                                text: "Highlight"
                                checkable: true
                                checked: root.highlight
                                enabled: root.nodeId.length > 0
                                tip: "Show what the selected node changes (⇧H)"
                                onClicked: root.setHighlight(!root.highlight)
                            }
                        }
                    }
                    PreviewPane {
                        id: viewer
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        project: root.project
                        playback: root.playback
                        canvasTools: false
                        onActivated: root.activated()
                        ViewerOverlay {
                            anchors.fill: parent
                            project: root.project
                            playback: root.playback
                            clipId: root.clipId
                            node: root.node
                            showWindow: root.centerPalette === "window" || root.node !== null && (root.node.window.shape || "").length > 0 && root.centerPalette !== "qualifier"
                            picking: root.picking
                            onPicked: root.picking = false
                        }
                    }
                }
            }

            NodeGraph {
                SplitView.preferredWidth: 270
                SplitView.minimumWidth: 200
                project: root.project
                clipId: root.gradable ? root.clipId : ""
                sel: root.sel
                nodeId: root.nodeId
                highlight: root.highlight
                onSelectNode: id => root.nodeId = id
                onHighlightToggled: root.setHighlight(!root.highlight)
                onOpenLooks: gallery.tab = "looks"
                onOpenEffects: {}
            }

            ColorSettings {
                SplitView.preferredWidth: 250
                SplitView.minimumWidth: 200
                project: root.project
                clipId: root.clipId
                sel: root.sel
            }
        }

        // ---- Bottom: clips, palette bar, palettes ------------------------------------------
        Rectangle {
            SplitView.preferredHeight: Math.max(360, Math.min(470, root.height * 0.5))
            SplitView.minimumHeight: 330
            color: "#121419"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Clips strip: click to grade that clip; right-click for grade copy and paste.
                ListView {
                    id: strip
                    objectName: "colorClips"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 70
                    orientation: ListView.Horizontal
                    spacing: 6
                    leftMargin: 8
                    rightMargin: 8
                    topMargin: 6
                    clip: true
                    model: root.clips
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property bool current: modelData.id === root.clipId
                        objectName: "colorClip-" + index
                        width: 104
                        height: 58
                        radius: 3
                        color: current ? Theme.accentSoft : "#1A1D23"
                        border.width: current ? 2 : 1
                        border.color: current ? Theme.accent : "#2A2E37"
                        Image {
                            anchors.fill: parent
                            anchors.margins: 2
                            anchors.bottomMargin: 16
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.height: 72
                            visible: (parent.modelData.media || "").length > 0
                            source: visible ? "image://thumbnail/" + encodeURIComponent(parent.modelData.media)
                                              + "?t=" + (parent.modelData.sourceIn + parent.modelData.duration / 2).toFixed(1) : ""
                        }
                        Label {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 3
                            text: (parent.index + 1).toString().padStart(2, "0") + "  " + parent.modelData.label + "  " + root.project.formatTime(parent.modelData.start)
                            color: parent.current ? Theme.text : Theme.textMuted
                            font.pixelSize: 9
                            font.family: Theme.monoFamily
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: mouse => {
                                root.activated()
                                root.gotoClip(parent.modelData)
                                if (mouse.button === Qt.RightButton) clipMenu.popup()
                            }
                        }
                    }
                }

                // Palette bar: the left palette, the center palettes, the node being graded.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    color: "#16181D"
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: "#23262E" }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#23262E" }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 2
                        PaletteTab { objectName: "palette-primaries"; text: "Primaries"; iconName: "adjust"; current: true }
                        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: "#2A2E37"; Layout.leftMargin: 6; Layout.rightMargin: 6 }
                        PaletteTab { objectName: "palette-curves"; text: "Curves"; iconName: "effects"; current: root.centerPalette === "curves"; onClicked: root.centerPalette = "curves" }
                        PaletteTab { objectName: "palette-qualifier"; text: "Qualifier"; iconName: "sparkle"; current: root.centerPalette === "qualifier"; onClicked: root.centerPalette = "qualifier" }
                        PaletteTab { objectName: "palette-window"; text: "Window"; iconName: "window"; current: root.centerPalette === "window"; onClicked: root.centerPalette = "window" }
                        PaletteTab { objectName: "palette-mask"; text: "Magic Mask"; iconName: "camera"; current: root.centerPalette === "mask"; onClicked: root.centerPalette = "mask" }
                        Item { Layout.fillWidth: true }
                        Rectangle {  // which node the palettes change
                            objectName: "editingNode"
                            Layout.preferredHeight: 22
                            Layout.preferredWidth: nodeChip.implicitWidth + 18
                            radius: 11
                            color: root.node ? Theme.accentSoft : "#1E2128"
                            border.color: root.node ? Theme.accent : "#30343E"
                            Label { id: nodeChip; anchors.centerIn: parent; text: "Node " + root.nodeName; color: Theme.text; font.pixelSize: 11 }
                        }
                        AbstractButton {
                            id: autoButton
                            objectName: "autoBalance"
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 22
                            enabled: root.gradable && !root.node && !!scopes.stats.meanR
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            onClicked: root.project.autoBalance(root.clipId, scopes.stats.meanR, scopes.stats.meanG, scopes.stats.meanB)
                            ToolTip.visible: hovered
                            ToolTip.text: "Auto Balance — remove the color cast"
                            ToolTip.delay: 400
                            background: Rectangle { radius: 11; color: autoButton.down ? Theme.pressed : autoButton.hovered ? Theme.hover : "#1E2128"; border.color: "#363B46" }
                            contentItem: Label { text: "A"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; color: autoButton.enabled ? Theme.text : Theme.textFaint; font.pixelSize: 11; font.weight: Font.Bold }
                        }
                        PaletteButton {
                            objectName: "resetGrade"
                            text: root.node ? "Reset node" : "Reset grade"
                            enabled: root.gradable
                            onClicked: root.resetGrade()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    PrimariesPalette {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        project: root.project
                        grade: root.grade
                        editable: root.gradable
                        onValueEdited: (key, v) => root.setValue(key, v)
                        onWheelEdited: (w, x, y, m) => root.setWheel(w, x, y, m)
                    }
                    Rectangle { Layout.fillHeight: true; width: 1; color: "#23262E" }
                    StackLayout {
                        Layout.fillHeight: true
                        Layout.fillWidth: false  // layouts fill by default; the wheels take the rest
                        Layout.preferredWidth: 430
                        Layout.minimumWidth: 340
                        currentIndex: ["curves", "qualifier", "window", "mask"].indexOf(root.centerPalette)
                        CurvesPalette {
                            project: root.project
                            playback: root.playback
                            grade: root.grade
                            editable: root.gradable
                            onCurveEdited: (channel, pts) => root.setCurve(channel, pts)
                        }
                        QualifierPalette {
                            project: root.project
                            playback: root.playback
                            clipId: root.clipId
                            node: root.node
                            picking: root.picking
                            highlight: root.highlight
                            onPickRequested: root.picking = !root.picking
                            onNodeCreated: id => root.nodeId = id
                        }
                        WindowPalette {
                            project: root.project
                            clipId: root.clipId
                            node: root.node
                            onNodeCreated: id => root.nodeId = id
                        }
                        MagicMaskPalette {
                            project: root.project
                            clipId: root.clipId
                            node: root.node
                            onNodeCreated: id => root.nodeId = id
                        }
                    }
                    Rectangle { Layout.fillHeight: true; width: 1; color: "#23262E" }
                    ScopesPanel {
                        id: scopes
                        Layout.fillHeight: true
                        Layout.preferredWidth: 360
                        Layout.minimumWidth: 260
                        playback: root.playback
                        editor: root.editor
                    }
                }
            }
        }
    }

    Menu {
        id: clipMenu
        MenuItem { text: "Copy Grade   ⌘C"; enabled: root.gradable; onTriggered: root.project.copyGrade(root.clipId) }
        MenuItem { text: "Paste Grade   ⌘V"; enabled: root.project.hasCopiedGrade; onTriggered: root.project.pasteGrade([root.clipId]) }
        MenuSeparator {}
        MenuItem { text: "Apply Grade of Previous Clip   ="; enabled: root.gradable; onTriggered: root.project.applyPreviousGrade(root.clipId) }
        MenuItem { text: "Apply Grade to Next Clip"; enabled: root.gradable; onTriggered: root.project.applyGradeToNext(root.clipId) }
        MenuItem { text: "Apply Grade to All Clips"; enabled: root.gradable; onTriggered: root.project.applyGradeToAll(root.clipId) }
        MenuSeparator {}
        MenuItem { text: "Grab Still   ⌥⌘G"; enabled: root.gradable; onTriggered: root.project.grabStill(root.clipId, root.playback.position) }
        MenuItem { text: "Reset Grade"; enabled: root.gradable; onTriggered: root.project.resetColor(root.clipId) }
    }

    component PaletteTab: AbstractButton {
        id: tab
        property string iconName
        property bool current: false
        Layout.preferredHeight: 28
        Layout.preferredWidth: tabRow.implicitWidth + 22
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            radius: 3
            color: tab.current ? "#262A33" : tab.hovered ? "#1E2128" : "transparent"
            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 2; color: Theme.record; visible: tab.current }
        }
        contentItem: Row {
            id: tabRow
            spacing: 6
            leftPadding: 11
            Icon { anchors.verticalCenter: parent.verticalCenter; name: tab.iconName; size: 13; color: tab.current ? Theme.text : Theme.textMuted }
            Label { anchors.verticalCenter: parent.verticalCenter; text: tab.text; color: tab.current ? Theme.text : Theme.textMuted; font.pixelSize: 11; font.weight: tab.current ? Font.DemiBold : Font.Normal }
        }
    }
}
