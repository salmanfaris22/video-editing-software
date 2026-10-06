import QtQuick
import QtQuick.Controls.Basic

// One clip on a track: click to select, drag its edges to trim, drag a free
// clip (text, overlay, music) to move it — along its layer or onto another
// layer of the same kind. Edges snap (⌘ held: no snapping); the edit applies
// when the drag ends. Drags keep working while the timeline auto-scrolls.
Item {
    id: root

    property var clip
    property bool video: true
    property bool locked: false
    property bool selected: false
    property real pxPerSecond: 40
    property real laneHeight: 48
    /// function(start, duration, clipId, modifiers) → snapped start (seconds)
    property var snapMove: null
    /// function(time, clipId, modifiers) → snapped time (seconds)
    property var snapEdge: null
    /// function(laneOffset) → whether this clip may be dropped that many lanes away
    property var acceptsLane: null
    /// Item the drag is measured in (it scrolls with the timeline content).
    property Item dragSpace: root.parent
    signal selectRequested(bool additive)
    signal trimRequested(string edge, real seconds)
    signal moveRequested(real seconds, int laneOffset)
    signal openRequested()
    /// Right-click: the context menu at a scene position.
    signal contextRequested(real sceneX, real sceneY)
    /// Pointer (scene coordinates) while dragging, for edge auto-scroll; ended() when released.
    signal dragPointer(real sceneX)
    signal dragEnded()
    signal laneHover(int laneOffset, bool ok)

    readonly property color baseColor: root.clip.color
    // Recording segments move too: the whole recording (every track) slides together.
    readonly property bool movable: !root.locked
    /// Offset while another part of the same recording is being dragged.
    property real groupShift: 0
    signal groupDrag(real dx)
    readonly property bool dragging: bodyMouse.moving || root.trimEdge !== ""
    property real moveDx: 0
    property int laneShift: 0
    property real trimDx: 0
    property string trimEdge: ""
    /// Width without the live trim, so thumbnails and waveform tiles are not re-requested on every pixel of a drag.
    readonly property real baseWidth: Math.max(4, root.clip.duration * root.pxPerSecond)
    readonly property real handleWidth: Math.max(3, Math.min(8, root.clip.duration * root.pxPerSecond / 4))

    x: root.clip.start * root.pxPerSecond + root.moveDx + (bodyMouse.moving ? 0 : root.groupShift)
       + (root.trimEdge === "start" ? root.trimDx : 0)
    // Moving onto another layer: the clip follows into that lane.
    transform: Translate { y: root.laneShift * root.laneHeight }
    z: root.dragging ? 30 : root.selected ? 2 : 1
    width: Math.max(4, root.clip.duration * root.pxPerSecond
                       + (root.trimEdge === "end" ? root.trimDx : root.trimEdge === "start" ? -root.trimDx : 0))
    opacity: root.clip.enabled ? (bodyMouse.moving ? 0.85 : 1) : 0.4

    /// Re-measures the drag after the pointer moved or the timeline scrolled.
    function updateDrag() {
        if (bodyMouse.pressed) bodyMouse.track()
        for (let i = 0; i < handles.count; ++i) {
            const h = handles.itemAt(i)
            if (h && h.pressed) h.track()
        }
    }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: 0
        color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b, root.selected ? 0.34 : root.video ? 0.18 : 0.24)
        border.width: root.selected ? 2 : 1
        border.color: root.selected ? Theme.accent : Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b, 0.75)
        clip: true

        // Content stays put in time while the start edge is trimmed.
        readonly property real contentShift: root.trimEdge === "start" ? -root.trimDx : 0
        Row {
            visible: root.video && root.clip.media.length > 0
            x: 1 + body.contentShift
            y: 1
            height: parent.height - 2
            opacity: 0.8
            Repeater {
                model: parent.visible ? Math.min(24, Math.max(1, Math.floor(root.baseWidth / 120))) : 0
                delegate: Image {
                    required property int index
                    readonly property int count: Math.min(24, Math.max(1, Math.floor(root.baseWidth / 120)))
                    width: (root.baseWidth - 2) / count
                    height: parent.height
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    sourceSize.height: 96
                    source: "image://thumbnail/" + encodeURIComponent(root.clip.media)
                            + "?t=" + (root.clip.sourceIn + (index + 0.5) * root.clip.duration / count).toFixed(1)
                }
            }
        }

        // Waveform, drawn in tiles so long or deeply zoomed clips stay cheap.
        Item {
            id: wave
            visible: !root.video && (root.clip.audioPath || "").length > 0
            x: body.contentShift
            width: root.baseWidth
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 22
            anchors.bottomMargin: 3
            readonly property int tile: 1024
            Repeater {
                model: wave.visible ? Math.ceil(root.baseWidth / wave.tile) : 0
                delegate: Image {
                    required property int index
                    readonly property real tileWidth: Math.min(wave.tile, root.baseWidth - index * wave.tile)
                    readonly property real secondsPerPx: root.clip.duration / Math.max(1, root.baseWidth)
                    x: index * wave.tile
                    width: tileWidth
                    height: wave.height
                    asynchronous: true
                    cache: true
                    fillMode: Image.Stretch
                    sourceSize: Qt.size(Math.max(2, Math.round(tileWidth)), Math.max(2, Math.round(wave.height)))
                    source: "image://waveform/" + encodeURIComponent(root.clip.audioPath)
                            + "?from=" + (root.clip.sourceIn + index * wave.tile * secondsPerPx).toFixed(4)
                            + "&to=" + (root.clip.sourceIn + (index * wave.tile + tileWidth) * secondsPerPx).toFixed(4)
                            + "&color=" + String(root.baseColor).substring(1, 7)
                    opacity: 0.85
                }
            }
        }

        // Fades (audio) as ramps at the clip edges.
        Canvas {
            visible: !root.video && (root.clip.fadeIn > 0 || root.clip.fadeOut > 0)
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = "rgba(255,255,255,0.55)"
                ctx.lineWidth = 1.5
                const fi = root.clip.fadeIn * root.pxPerSecond
                const fo = root.clip.fadeOut * root.pxPerSecond
                ctx.beginPath()
                ctx.moveTo(0, height - 4)
                ctx.lineTo(fi, 4)
                ctx.lineTo(width - fo, 4)
                ctx.lineTo(width, height - 4)
                ctx.stroke()
            }
            onWidthChanged: requestPaint()
        }
        Rectangle {
            visible: !root.video && !wave.visible
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 8
            height: 1
            color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b, 0.6)
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 4
            width: Math.min(parent.width - 8, label.implicitWidth + 10)
            height: 16
            radius: 0
            color: Qt.rgba(0, 0, 0, 0.55)
            visible: root.selected && root.width > 28
            Label {
                id: label
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                verticalAlignment: Text.AlignVCenter
                text: (root.clip.muted ? "Muted · " : "") + root.clip.name
                color: "white"
                font.pixelSize: 9
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: bodyMouse
        anchors.fill: parent
        anchors.leftMargin: root.handleWidth
        anchors.rightMargin: root.handleWidth
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: root.movable && pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        property point pressPoint
        property point scenePoint
        property int mods: 0
        property bool moving: false
        onPressed: mouse => {
            scenePoint = mapToItem(null, mouse.x, mouse.y)
            if (mouse.button === Qt.RightButton) {
                // Like Resolve: right-click keeps a multi-selection that includes this clip.
                if (!root.selected) root.selectRequested(false)
                root.contextRequested(scenePoint.x, scenePoint.y)
                return
            }
            pressPoint = root.dragSpace.mapFromItem(null, scenePoint.x, scenePoint.y)
            moving = false
            const additive = mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)
            root.selectRequested(additive)
        }
        onPositionChanged: mouse => {
            if (!pressed || !root.movable || (pressedButtons & Qt.RightButton)) return
            scenePoint = mapToItem(null, mouse.x, mouse.y)
            mods = mouse.modifiers
            track()
            root.dragPointer(scenePoint.x)
        }
        function track() {
            const p = root.dragSpace.mapFromItem(null, scenePoint.x, scenePoint.y)
            const dx = p.x - pressPoint.x
            const dy = p.y - pressPoint.y
            if (!moving && dx * dx + dy * dy < 16) return  // a click, not a drag
            moving = true
            let start = Math.max(0, root.clip.start + dx / root.pxPerSecond)
            if (root.snapMove) start = root.snapMove(start, root.clip.duration, root.clip.id, mods)
            root.moveDx = (start - root.clip.start) * root.pxPerSecond
            if (root.clip.linked) root.groupDrag(root.moveDx)
            // A recording segment stays on its tracks (it only slides in time).
            const offset = root.clip.linked ? 0 : Math.round(dy / Math.max(1, root.laneHeight))
            const ok = offset === 0 || (root.acceptsLane ? root.acceptsLane(offset) : false)
            root.laneShift = ok ? offset : 0
            root.laneHover(offset, ok)
        }
        onReleased: mouse => finish(mouse.button === Qt.LeftButton)
        onCanceled: finish(false)
        function finish(apply) {
            const dx = root.moveDx
            const lanes = root.laneShift
            const wasMoving = moving
            root.moveDx = 0
            root.laneShift = 0
            moving = false
            root.dragEnded()
            if (apply && wasMoving && (Math.abs(dx) > 0.5 || lanes !== 0))
                root.moveRequested(Math.max(0, root.clip.start + dx / root.pxPerSecond), lanes)
        }
        onDoubleClicked: root.openRequested()
    }

    // Trim handles.
    Repeater {
        id: handles
        model: root.locked ? [] : ["start", "end"]
        delegate: MouseArea {
            id: handle
            required property string modelData
            width: root.handleWidth
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: modelData === "start" ? 0 : root.width - width
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            property real pressX: 0
            property real sceneX: 0
            property int mods: 0
            onPressed: mouse => {
                sceneX = mapToItem(null, mouse.x, 0).x
                pressX = root.dragSpace.mapFromItem(null, sceneX, 0).x
                root.trimEdge = modelData
                root.selectRequested(false)
            }
            onPositionChanged: mouse => {
                if (!pressed) return
                sceneX = mapToItem(null, mouse.x, 0).x
                mods = mouse.modifiers
                track()
                root.dragPointer(sceneX)
            }
            function track() {
                const dx = root.dragSpace.mapFromItem(null, sceneX, 0).x - pressX
                const full = root.clip.duration * root.pxPerSecond
                const edge0 = modelData === "start" ? root.clip.start : root.clip.start + root.clip.duration
                let edge = edge0 + dx / root.pxPerSecond
                if (root.snapEdge) edge = root.snapEdge(edge, root.clip.id, mods)
                const snapped = (edge - edge0) * root.pxPerSecond
                // Keep at least a few pixels of clip while dragging.
                root.trimDx = modelData === "start" ? Math.min(snapped, full - 4) : Math.max(snapped, -full + 4)
            }
            onReleased: finish(true)
            onCanceled: finish(false)
            function finish(apply) {
                const dx = root.trimDx
                root.trimDx = 0
                root.trimEdge = ""
                root.dragEnded()
                if (!apply || Math.abs(dx) < 0.5) return
                const edge = modelData === "start" ? root.clip.start + dx / root.pxPerSecond
                                                   : root.clip.start + root.clip.duration + dx / root.pxPerSecond
                root.trimRequested(modelData, edge)
            }
            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                radius: 0
                color: Theme.accent
                opacity: handle.containsMouse || handle.pressed ? 0.9 : root.selected ? 0.35 : 0
            }
        }
    }
}
