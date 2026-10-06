import QtQuick
import QtQuick.Controls.Basic

// Direct manipulation on the preview canvas: click a layer (screen, camera,
// overlay, text, subtitle) to select it, drag it to move it — it snaps to the
// canvas center and edges (hold ⌘ to place freely) — and drag a handle to
// resize it. Corner handles keep the picture's proportions; the camera's edge
// handles reshape it. ⌥ resizes from the center. Double-click text to edit it
// in place. While dragging only the preview follows; releasing commits one
// undo step. Esc cancels a drag.
Item {
    id: root

    property ProjectController project
    property PlaybackController playback
    signal activated()
    signal emptyClicked()

    readonly property string selected: root.project.selectedClip
    property var box: ({})          // the selected layer, canvas fractions (from the document)
    property var hoverBox: ({})     // the layer under the pointer
    property string hoverClip: ""
    property var live: null         // the rect while dragging (fractions)
    property bool dragging: false
    readonly property var shown: root.dragging && root.live ? root.live : root.box
    readonly property bool hasBox: root.shown && root.shown.w !== undefined
    property real guideX: -1        // snapping guides (fractions), −1 = none
    property real guideY: -1
    property bool editing: false

    readonly property real handleSize: 9
    readonly property real snapPixels: 6

    function refresh() {
        if (root.dragging) return
        root.box = root.selected.length > 0 ? root.project.layerBox(root.selected, root.playback.position) : ({})
    }
    onSelectedChanged: refresh()
    Connections {
        target: root.project
        function onProjectChanged() { root.refresh() }
        function onSelectionChanged() { root.refresh() }
    }
    Connections {
        target: root.playback
        function onPositionChanged() { root.refresh() }
    }
    Component.onCompleted: refresh()

    // ---- geometry helpers (pixels ↔ fractions) ------------------------------
    function px(r) { return Qt.rect(r.x * width, r.y * height, r.w * width, r.h * height) }
    function frac(r) { return { x: r.x / width, y: r.y / height, w: r.width / width, h: r.height / height } }

    /// Handle under a point of the selection frame: "tl", "t", "tr", "r", "br", "b", "bl", "l" or "".
    function handleAt(mx, my) {
        if (!root.hasBox || root.editing) return ""
        const r = px(root.shown)
        const pts = {
            tl: [r.x, r.y], tr: [r.x + r.width, r.y], bl: [r.x, r.y + r.height], br: [r.x + r.width, r.y + r.height]
        }
        if (root.shown.edges) {
            pts.t = [r.x + r.width / 2, r.y]
            pts.b = [r.x + r.width / 2, r.y + r.height]
            pts.l = [r.x, r.y + r.height / 2]
            pts.r = [r.x + r.width, r.y + r.height / 2]
        }
        for (const k in pts) {
            if (Math.abs(mx - pts[k][0]) <= root.handleSize && Math.abs(my - pts[k][1]) <= root.handleSize) return k
        }
        return ""
    }

    function cursorFor(handle) {
        switch (handle) {
        case "tl": case "br": return Qt.SizeFDiagCursor
        case "tr": case "bl": return Qt.SizeBDiagCursor
        case "t": case "b": return Qt.SizeVerCursor
        case "l": case "r": return Qt.SizeHorCursor
        }
        return Qt.ArrowCursor
    }

    /// Moves `r` (pixels) so its center or edges stick to the canvas center
    /// and edges within a few pixels; sets the guides.
    function snapMove(r) {
        root.guideX = -1
        root.guideY = -1
        const xs = [[r.x + r.width / 2, width / 2], [r.x, 0], [r.x + r.width, width]]
        for (const [value, target] of xs) {
            if (Math.abs(value - target) <= root.snapPixels) {
                r.x += target - value
                root.guideX = target / width
                break
            }
        }
        const ys = [[r.y + r.height / 2, height / 2], [r.y, 0], [r.y + r.height, height]]
        for (const [value, target] of ys) {
            if (Math.abs(value - target) <= root.snapPixels) {
                r.y += target - value
                root.guideY = target / height
                break
            }
        }
        return r
    }

    /// The rect after dragging `handle` of `o` (pixels) by (dx, dy).
    function resized(o, handle, dx, dy, aspect, fromCenter) {
        const minSize = 16
        const left = handle.indexOf("l") >= 0, right = handle.indexOf("r") >= 0
        const top = handle.indexOf("t") >= 0, bottom = handle.indexOf("b") >= 0
        const corner = (left || right) && (top || bottom)
        if (corner && aspect > 0) {
            // The dragged corner follows the pointer along the diagonal.
            const ax = left ? o.x + o.width : o.x, ay = top ? o.y + o.height : o.y
            const cx = left ? o.x : o.x + o.width, cy = top ? o.y : o.y + o.height
            const vx = cx - ax, vy = cy - ay
            const scaleFactor = fromCenter ? 2 : 1
            let s = 1 + scaleFactor * ((dx * vx) + (dy * vy)) / (vx * vx + vy * vy)
            s = Math.max(s, minSize / Math.min(o.width, o.height))
            const w = o.width * s, h = o.height * s
            if (fromCenter) return Qt.rect(o.x + (o.width - w) / 2, o.y + (o.height - h) / 2, w, h)
            return Qt.rect(left ? ax - w : ax, top ? ay - h : ay, w, h)
        }
        let x0 = o.x, y0 = o.y, x1 = o.x + o.width, y1 = o.y + o.height
        const k = fromCenter ? 1 : 0
        if (left) { x0 += dx; x1 -= k * dx }
        if (right) { x1 += dx; x0 -= k * dx }
        if (top) { y0 += dy; y1 -= k * dy }
        if (bottom) { y1 += dy; y0 -= k * dy }
        if (x1 - x0 < minSize) { if (left) x0 = x1 - minSize; else x1 = x0 + minSize }
        if (y1 - y0 < minSize) { if (top) y0 = y1 - minSize; else y1 = y0 + minSize }
        return Qt.rect(x0, y0, x1 - x0, y1 - y0)
    }

    function sendPreview() {
        if (root.dragging && root.live)
            root.project.previewLayerRect(dragArea.clip, dragArea.time, root.live.x, root.live.y, root.live.w, root.live.h)
    }

    function cancelDrag() {
        if (!root.dragging) return
        previewTimer.stop()
        root.dragging = false
        root.live = null
        root.guideX = -1
        root.guideY = -1
        root.project.cancelPreview()
        root.refresh()
    }

    function beginTextEdit() {
        if (!root.hasBox || root.project.selection.role !== "text") return
        root.playback.pause()
        root.editing = true
        textEditor.open(root.project.selection.text || "")
    }

    Timer {
        id: previewTimer
        interval: 33
        repeat: true
        onTriggered: root.sendPreview()
    }

    // ---- interaction ------------------------------------------------------------
    MouseArea {
        id: dragArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        enabled: !root.editing
        cursorShape: handle.length > 0 ? root.cursorFor(handle)
                                       : root.hoverClip.length > 0 ? Qt.SizeAllCursor : Qt.ArrowCursor

        property string mode: ""     // "move", "resize" or ""
        property string handle: ""   // handle under the pointer / being dragged
        property string clip: ""
        property real time: 0
        property point start
        property rect origin
        property bool moved: false
        property bool pressedEmpty: false

        onPositionChanged: mouse => {
            if (!pressed) {
                handle = root.handleAt(mouse.x, mouse.y)
                const hit = handle.length > 0 ? root.selected
                                              : root.project.layerAt(mouse.x / width, mouse.y / height, root.playback.position)
                if (hit !== root.hoverClip) {
                    root.hoverClip = hit
                    root.hoverBox = hit.length > 0 && hit !== root.selected
                                  ? root.project.layerBox(hit, root.playback.position) : ({})
                }
                return
            }
            if (mode === "") return
            const dx = mouse.x - start.x
            const dy = mouse.y - start.y
            if (!moved && Math.abs(dx) + Math.abs(dy) < 3) return
            if (!moved) {
                moved = true
                root.dragging = true
                root.playback.pause()
                previewTimer.start()
            }
            const free = (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
            let r
            if (mode === "move") {
                r = Qt.rect(origin.x + (root.box.vertical ? 0 : dx), origin.y + dy, origin.width, origin.height)
                if (!free) r = root.snapMove(r)
                else { root.guideX = -1; root.guideY = -1 }
            } else {
                const lock = root.box.edges && handle.length === 1 ? 0 : (root.box.aspect || 0)
                r = root.resized(origin, handle, dx, dy, lock, (mouse.modifiers & Qt.AltModifier) !== 0)
                root.guideX = -1
                root.guideY = -1
            }
            const f = root.frac(r)
            f.circle = root.box.circle
            f.edges = root.box.edges
            f.vertical = root.box.vertical
            f.aspect = root.box.aspect
            f.role = root.box.role
            root.live = f
        }

        onPressed: mouse => {
            root.activated()
            moved = false
            pressedEmpty = false
            mode = ""
            start = Qt.point(mouse.x, mouse.y)
            time = root.playback.position
            handle = root.handleAt(mouse.x, mouse.y)
            if (handle.length > 0) {
                mode = "resize"
                clip = root.selected
            } else {
                const hit = root.project.layerAt(mouse.x / width, mouse.y / height, time)
                if (hit.length === 0) {
                    pressedEmpty = true
                    return
                }
                if (hit !== root.selected) root.project.selectClip(hit)
                clip = hit
                mode = "move"
                root.box = root.project.layerBox(hit, time)
            }
            origin = root.px(root.box)
            if (mouse.button === Qt.RightButton) {
                mode = ""
                contextMenu.popup()
            }
        }

        onReleased: mouse => {
            if (root.dragging && root.live) {
                previewTimer.stop()
                const r = root.live
                root.project.setLayerRect(clip, time, r.x, r.y, r.w, r.h)
                root.dragging = false
                root.live = null
                root.guideX = -1
                root.guideY = -1
                root.refresh()
            }
            mode = ""
        }

        onClicked: mouse => {
            if (mouse.button !== Qt.LeftButton || moved || !pressedEmpty) return
            if (root.selected.length > 0) root.project.clearSelection()
            else root.emptyClicked()
        }

        onDoubleClicked: mouse => {
            if (root.project.selection.role === "text") root.beginTextEdit()
        }

        onExited: {
            root.hoverClip = ""
            root.hoverBox = ({})
        }
    }

    // ---- visuals ------------------------------------------------------------------
    // The layer that a click would pick.
    Rectangle {
        visible: root.hoverBox.w !== undefined && !root.dragging && !root.editing
        x: (root.hoverBox.x || 0) * root.width
        y: (root.hoverBox.y || 0) * root.height
        width: (root.hoverBox.w || 0) * root.width
        height: (root.hoverBox.h || 0) * root.height
        radius: root.hoverBox.circle ? width / 2 : 2
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.55)
    }

    // Snapping guides.
    Rectangle {
        visible: root.guideX >= 0
        x: root.guideX * root.width - 0.5
        width: 1
        height: root.height
        color: "#FF4FD1"
    }
    Rectangle {
        visible: root.guideY >= 0
        y: root.guideY * root.height - 0.5
        height: 1
        width: root.width
        color: "#FF4FD1"
    }

    // The selected layer: outline, handles and its size in canvas pixels.
    Item {
        id: frame
        visible: root.hasBox && !root.editing
        x: (root.shown.x || 0) * root.width
        y: (root.shown.y || 0) * root.height
        width: (root.shown.w || 0) * root.width
        height: (root.shown.h || 0) * root.height

        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 1.5
            border.color: Theme.accent
            radius: 1
        }
        Rectangle {
            visible: root.shown.circle === true
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 1.5
            border.color: Theme.accent
        }
        Repeater {
            model: {
                const list = [[0, 0], [1, 0], [0, 1], [1, 1]]
                if (root.shown.edges) list.push([0.5, 0], [0.5, 1], [0, 0.5], [1, 0.5])
                return list
            }
            delegate: Rectangle {
                required property var modelData
                x: modelData[0] * frame.width - width / 2
                y: modelData[1] * frame.height - height / 2
                width: root.handleSize
                height: root.handleSize
                radius: 2
                color: "white"
                border.width: 1.5
                border.color: Theme.accent
            }
        }
        Rectangle {
            visible: root.dragging || dragArea.containsMouse
            anchors.left: parent.left
            anchors.bottom: parent.top
            anchors.bottomMargin: 6
            width: sizeLabel.implicitWidth + 12
            height: 20
            radius: 4
            color: Qt.rgba(0.05, 0.06, 0.08, 0.85)
            Label {
                id: sizeLabel
                anchors.centerIn: parent
                readonly property string roleName: ({ screen: "Screen", camera: "Camera", overlay: "Overlay",
                                                       text: "Text", subtitle: "Subtitles" })[root.shown.role] || ""
                text: roleName + " · " + Math.round((root.shown.w || 0) * root.project.canvasWidth) + " × "
                      + Math.round((root.shown.h || 0) * root.project.canvasHeight)
                color: Theme.text
                font.pixelSize: Theme.fontXS
                font.family: Theme.monoFamily
            }
        }
    }

    InlineTextEditor {
        id: textEditor
        x: ((root.box.x || 0) + (root.box.w || 0) / 2) * root.width - width / 2
        y: ((root.box.y || 0) + (root.box.h || 0) / 2) * root.height - height / 2
        minWidth: (root.box.w || 0) * root.width
        canvasHeight: root.height
        project: root.project
        onFinished: (accepted, text) => {
            root.editing = false
            if (accepted && text !== root.project.selection.text) root.project.setText(root.selected, text)
            root.activated()
        }
    }

    Menu {
        id: contextMenu
        MenuItem {
            text: "Edit text"
            visible: root.project.selection.role === "text"
            height: visible ? implicitHeight : 0
            onTriggered: root.beginTextEdit()
        }
        MenuItem {
            text: "Reset position and size"
            onTriggered: root.project.resetLayerRect(root.selected, root.playback.position)
        }
        MenuItem {
            text: root.project.selection.role === "screen" || root.project.selection.role === "camera"
                  ? "Reset whole layout" : "Delete"
            visible: root.project.selection.role !== "subtitle"
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (root.project.selection.role === "screen" || root.project.selection.role === "camera")
                    root.project.resetLayoutCustomization(root.playback.position)
                else
                    root.project.deleteSelected()
            }
        }
    }

    // Esc while dragging puts the layer back.
    Shortcut {
        sequence: "Escape"
        enabled: root.dragging
        onActivated: root.cancelDrag()
    }
}
