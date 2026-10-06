import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Multi-track timeline: ruler (click/drag to scrub), layout sections, track
// lanes (layers) with selectable/trimmable/movable clips, markers, in/out
// range, detected pauses and a draggable playhead. Ruler and headers stay put
// while lanes scroll. Scrubbing and clip drags snap to clip edges, markers and
// the playhead (⌘ held: free) and auto-scroll at the edges; the wheel or
// trackpad scrolls, ⌘/Ctrl+wheel or pinch zooms around the pointer.
Rectangle {
    id: root

    property ProjectController project
    property PlaybackController playback
    property var editor  // EditorScreen: markIn, markOut, tool
    property real pxPerSecond: 36
    readonly property int headerWidth: 196
    readonly property int rulerHeight: 26
    readonly property int layoutHeight: 18
    readonly property int trackHeight: Math.round(root.editor ? root.editor.timelineTrackHeight : 48)
    readonly property string gridMode: root.editor ? root.editor.timelineGridMode : "auto"
    readonly property real playhead: root.playback.position
    readonly property real timelineWidth: Math.max(lanes.width, (root.project.duration + 5) * root.pxPerSecond)
    signal activated()

    property bool snapping: true
    property bool scrubbing: false
    property bool resumeAfterScrub: false
    property real scrubSceneX: 0
    property int scrubMods: 0
    // Lane a clip is being dragged over (-1: none) and whether it can land there.
    // A recording segment being dragged: its other tracks follow (link group, offset in px).
    property string groupDragGroup: ""
    property real groupDragDx: 0
    property int dropLane: -1
    property bool dropOk: false
    // Edge auto-scroll while dragging (pixels per tick, signed).
    property real edgeVelocity: 0
    signal autoScrolled()
    readonly property real snapPx: 8

    property bool marqueeActive: false
    property real marqueeX0: 0
    property real marqueeY0: 0
    property real marqueeX1: 0
    property real marqueeY1: 0

    color: Theme.surface

    readonly property string dragSelectMode: root.editor ? root.editor.dragSelectMode : "drag"
    readonly property string marqueeMatch: root.editor ? root.editor.marqueeMatch : "touch"
    readonly property string linkedEditMode: root.editor ? root.editor.linkedEditMode : "track"
    /// "linked": a recording segment moves on every track; "one": only the dragged clip (⌥ toggles while dragging).
    readonly property string moveMode: root.editor && root.editor.timelineMoveMode ? root.editor.timelineMoveMode : "linked"
    readonly property bool cutAllTracks: root.linkedEditMode === "allTracks"
    readonly property int clipInset: 5

    FileDialog {
        id: audioDialog
        title: "Add audio"
        nameFilters: ["Audio (*.mp3 *.m4a *.aac *.wav *.aif *.aiff *.flac *.ogg *.opus)"]
        onAccepted: root.project.importMedia(selectedFile, "music", root.playhead)
    }
    FileDialog {
        id: mediaDialog
        title: "Add an image or video layer"
        nameFilters: ["Images and videos (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.tif *.tiff *.heic *.mp4 *.mov *.m4v *.mkv *.webm *.avi)"]
        onAccepted: root.project.importMedia(selectedFile, "overlay", root.playhead)
    }

    /// Adds a layer of `what` at the playhead and opens its tool.
    function addLayer(what) {
        root.activated()
        switch (what) {
        case "text":
            root.project.addText("Your text", root.playhead, 4.0, "title")
            if (root.editor) root.editor.tool = "style"
            break
        case "media": mediaDialog.open(); break
        case "audio": audioDialog.open(); break
        case "caption":
            root.project.addSubtitle("Caption", root.playhead, 3.0)
            if (root.editor) root.editor.tool = "subtitles"
            break
        case "empty": root.project.addTrack("overlay"); break
        case "empty-audio": root.project.addTrack("audio"); break
        }
    }
    // ---- Right-click menus (Resolve / After Effects style) -----------------------
    property var menuClip: ({})        // the clip right-clicked
    property string menuTrackKind: ""
    property real menuTime: 0          // timeline time under the pointer
    function clipAt(id) {
        for (const track of root.project.tracks)
            for (const clip of track.clips)
                if (clip.id === id) return { clip: clip, track: track }
        return null
    }
    Menu {
        id: clipMenu
        objectName: "clipMenu"
        MenuItem {
            text: "Split at playhead\tS"
            enabled: root.menuClip.start !== undefined && root.playhead > root.menuClip.start && root.playhead < root.menuClip.start + root.menuClip.duration
            onTriggered: root.project.splitAt(root.playhead)
        }
        MenuItem { text: "Delete (leave gap)\t⌫"; onTriggered: { root.project.linkedEditMode = "track"; root.project.deleteSelected() } }
        MenuItem {
            text: "Unlink from Recording (move on its own)"
            enabled: root.menuClip.linked === true
            onTriggered: root.project.unlinkClip(root.menuClip.id)
        }
        MenuItem {
            text: "Ripple delete (close gap)\t⇧⌫"
            onTriggered: root.project.removeRange(root.menuClip.start, root.menuClip.start + root.menuClip.duration)
        }
        MenuSeparator {}
        MenuItem {
            text: root.menuClip.enabled === false ? "Enable clip\tD" : "Disable clip\tD"
            onTriggered: root.project.setClipEnabled(root.menuClip.id, root.menuClip.enabled === false)
        }
        MenuItem {
            text: root.menuClip.muted ? "Unmute audio" : "Mute audio"
            visible: (root.menuClip.audioPath || "").length > 0
            height: visible ? implicitHeight : 0
            onTriggered: root.project.setClipAudio(root.menuClip.id, "muted", !root.menuClip.muted)
        }
        MenuItem {
            text: "Select all on this layer"
            onTriggered: {
                const found = root.clipAt(root.menuClip.id)
                if (found) root.project.selectClips(found.track.clips.map(c => c.id))
            }
        }
        MenuSeparator {}
        MenuItem {
            text: "Grade this clip\t⇧6"
            visible: root.menuTrackKind !== "audio" && root.menuClip.role !== "text" && root.menuClip.role !== "subtitle"
            height: visible ? implicitHeight : 0
            onTriggered: {
                root.project.selectClip(root.menuClip.id)
                if (root.editor) root.editor.page = "color"
            }
        }
        MenuItem {
            text: "Properties…"
            onTriggered: {
                if (!root.editor) return
                const r = root.menuClip.role
                root.editor.tool = r === "text" ? "style" : r === "subtitle" ? "subtitles" : r === "overlay" ? "overlay"
                                 : root.menuTrackKind === "audio" ? "audio" : "effects"
            }
        }
    }
    Menu {
        id: laneMenu
        objectName: "laneMenu"
        MenuItem { text: "Add marker here\tM"; onTriggered: root.project.addMarker(root.menuTime, "") }
        MenuItem { text: "Add text here"; onTriggered: { root.project.addText("Your text", root.menuTime, 4.0, "title"); if (root.editor) root.editor.tool = "style" } }
        MenuItem { text: "Split all tracks here"; onTriggered: { root.project.clearSelection(); root.project.splitAt(root.menuTime) } }
        MenuSeparator {}
        MenuItem { text: "Add layer…"; onTriggered: addLayerMenu.popup() }
        MenuItem { text: "Select all clips\t⌘A"; onTriggered: root.project.selectAllClips() }
    }
    Menu {
        id: rulerMenu
        objectName: "rulerMenu"
        MenuItem { text: "Add marker\tM"; onTriggered: root.project.addMarker(root.menuTime, "") }
        MenuItem { text: "Set in point here\tI"; enabled: !!root.editor; onTriggered: { root.editor.markIn = root.menuTime; if (root.editor.markOut <= root.menuTime) root.editor.markOut = -1 } }
        MenuItem { text: "Set out point here\tO"; enabled: !!root.editor; onTriggered: { root.editor.markOut = root.menuTime; if (root.editor.markIn < 0 || root.editor.markIn >= root.menuTime) root.editor.markIn = 0 } }
        MenuItem { text: "Fit timeline to window"; onTriggered: root.fitToWidth() }
    }

    Menu {
        id: addLayerMenu
        objectName: "addLayerMenu"
        MenuItem { text: "Text"; onTriggered: root.addLayer("text") }
        MenuItem { text: "Image or video…"; enabled: !root.project.busy; onTriggered: root.addLayer("media") }
        MenuItem { text: "Music or sound…"; enabled: !root.project.busy; onTriggered: root.addLayer("audio") }
        MenuItem { text: "Caption"; onTriggered: root.addLayer("caption") }
        MenuSeparator {}
        MenuItem { text: "Empty layer"; onTriggered: root.addLayer("empty") }
        MenuItem { text: "Empty audio layer"; onTriggered: root.addLayer("empty-audio") }
    }

    function clipIdsInRect(x0, y0, x1, y1) {
        const left = Math.min(x0, x1)
        const right = Math.max(x0, x1)
        const top = Math.min(y0, y1)
        const bottom = Math.max(y0, y1)
        const inside = root.marqueeMatch === "inside"
        const ids = []
        const tracks = root.project.tracks
        for (let ti = 0; ti < tracks.length; ++ti) {
            const laneTop = ti * root.trackHeight + root.clipInset
            const laneBottom = (ti + 1) * root.trackHeight - root.clipInset
            if (laneBottom < top || laneTop > bottom) continue
            for (let ci = 0; ci < tracks[ti].clips.length; ++ci) {
                const clip = tracks[ti].clips[ci]
                const cx = clip.start * root.pxPerSecond
                const cr = cx + clip.duration * root.pxPerSecond
                if (inside) {
                    if (cx >= left && cr <= right && laneTop >= top && laneBottom <= bottom) ids.push(clip.id)
                } else if (cr >= left && cx <= right) {
                    ids.push(clip.id)
                }
            }
        }
        return ids
    }

    function beginMarquee(x, y) {
        root.marqueeActive = true
        root.marqueeX0 = x
        root.marqueeY0 = y
        root.marqueeX1 = x
        root.marqueeY1 = y
    }
    function updateMarquee(x, y) {
        root.marqueeX1 = x
        root.marqueeY1 = y
    }
    function finishMarquee(additive, subtract) {
        const ids = root.clipIdsInRect(root.marqueeX0, root.marqueeY0, root.marqueeX1, root.marqueeY1)
        if (subtract) {
            const left = root.project.selectedClips.filter(id => ids.indexOf(id) < 0)
            root.project.selectClips(left)
        } else if (ids.length === 0 && !additive) {
            root.project.clearSelection()
        } else if (additive) {
            const merged = root.project.selectedClips.slice()
            for (let i = 0; i < ids.length; ++i)
                if (merged.indexOf(ids[i]) < 0) merged.push(ids[i])
            root.project.selectClips(merged)
        } else {
            root.project.selectClips(ids)
        }
        root.marqueeActive = false
    }

    function timeAt(x) { return Math.max(0, Math.min(root.project.duration, x / root.pxPerSecond)) }
    function frameTime(t) {
        const fps = Math.max(1, root.project.frameRate)
        return Math.round(t * fps) / fps
    }
    /// Timeline x (content coordinates) of a scene x.
    function contentXAt(sceneX) { return lanes.contentX + lanes.mapFromItem(null, sceneX, 0).x }

    // ---- Snapping -------------------------------------------------------------
    function linkGroupOf(clipId) {
        const tracks = root.project.tracks
        for (let ti = 0; ti < tracks.length; ++ti)
            for (let ci = 0; ci < tracks[ti].clips.length; ++ci)
                if (tracks[ti].clips[ci].id === clipId) return tracks[ti].clips[ci].linkGroup || ""
        return ""
    }
    function snapPoints(excludeId, withPlayhead) {
        const pts = [0, root.project.duration]
        if (withPlayhead) pts.push(root.playhead)
        const group = excludeId ? root.linkGroupOf(excludeId) : ""
        const tracks = root.project.tracks
        for (let ti = 0; ti < tracks.length; ++ti) {
            const clips = tracks[ti].clips
            for (let ci = 0; ci < clips.length; ++ci) {
                if (clips[ci].id === excludeId) continue
                if (group !== "" && clips[ci].linkGroup === group) continue  // the recording being moved
                pts.push(clips[ci].start, clips[ci].start + clips[ci].duration)
            }
        }
        const markers = root.project.markers
        for (let i = 0; i < markers.length; ++i) pts.push(markers[i].time)
        if (root.editor && root.editor.markIn >= 0) pts.push(root.editor.markIn)
        if (root.editor && root.editor.markOut >= 0) pts.push(root.editor.markOut)
        return pts
    }
    function snapFree(mods) { return !root.snapping || (mods & (Qt.ControlModifier | Qt.MetaModifier)) }
    /// The nearest snap point within snapPx of `t` (or `t`), and how far it moved.
    function nearest(t, pts) {
        let best = t
        let dist = root.snapPx / root.pxPerSecond
        for (let i = 0; i < pts.length; ++i) {
            const d = Math.abs(pts[i] - t)
            if (d < dist) { dist = d; best = pts[i] }
        }
        return best
    }
    function snapEdge(t, clipId, mods) {
        return root.snapFree(mods) ? t : root.nearest(t, root.snapPoints(clipId, true))
    }
    function snapMove(start, duration, clipId, mods) {
        if (root.snapFree(mods)) return Math.max(0, start)
        const pts = root.snapPoints(clipId, true)
        const s = root.nearest(start, pts)
        const e = root.nearest(start + duration, pts)
        // Whichever edge is closer to a snap point wins.
        const ds = s !== start ? Math.abs(s - start) : Infinity
        const de = e !== start + duration ? Math.abs(e - start - duration) : Infinity
        if (ds === Infinity && de === Infinity) return Math.max(0, start)
        return Math.max(0, ds <= de ? s : e - duration)
    }

    // ---- Scrubbing ------------------------------------------------------------
    function beginScrub(sceneX, mods) {
        root.activated()
        root.scrubbing = true
        root.resumeAfterScrub = root.playback.playing
        if (root.resumeAfterScrub) root.playback.pause()
        root.scrubTo(sceneX, mods)
    }
    function scrubTo(sceneX, mods) {
        root.scrubSceneX = sceneX
        root.scrubMods = mods
        let t = root.timeAt(root.contentXAt(sceneX))
        if (!root.snapFree(mods)) t = root.nearest(t, root.snapPoints("", false))
        root.playback.seek(root.frameTime(t))
        root.edgeDrag(sceneX)
    }
    function endScrub() {
        if (!root.scrubbing) return
        root.scrubbing = false
        root.stopEdgeDrag()
        if (root.resumeAfterScrub) root.playback.play()
        root.resumeAfterScrub = false
    }

    // ---- Edge auto-scroll -----------------------------------------------------
    function edgeDrag(sceneX) {
        const x = lanes.mapFromItem(null, sceneX, 0).x
        const zone = Math.min(48, lanes.width / 6)
        if (x < zone && lanes.contentX > 0) root.edgeVelocity = -Math.min(30, (zone - x) / 2 + 2)
        else if (x > lanes.width - zone) root.edgeVelocity = Math.min(30, (x - lanes.width + zone) / 2 + 2)
        else root.edgeVelocity = 0
    }
    function stopEdgeDrag() { root.edgeVelocity = 0 }
    function scrollBy(dx, dy) {
        if (dx !== 0) lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width, lanes.contentX + dx))
        if (dy !== 0) lanes.contentY = Math.max(0, Math.min(lanes.contentHeight - lanes.height, lanes.contentY + dy))
    }
    function wheel(event) {
        if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) {
            const d = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 4
            root.zoomAround(Math.pow(1.006, d), Math.max(0, Math.min(lanes.width, root.wheelAnchor)))
            return
        }
        let dx = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0 ? event.pixelDelta.x : event.angleDelta.x / 2
        let dy = event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 2
        if (event.modifiers & Qt.ShiftModifier && dx === 0) { dx = dy; dy = 0 }
        // A plain vertical wheel scrolls through time when every layer fits.
        if (Math.abs(dy) > Math.abs(dx) && lanes.contentHeight <= lanes.height + 1) { dx = dy; dy = 0 }
        root.scrollBy(-dx, -dy)
    }
    property real wheelAnchor: 0

    // ---- Layers ---------------------------------------------------------------
    function laneAccepts(fromIndex, offset) {
        const tracks = root.project.tracks
        const to = fromIndex + offset
        if (to < 0 || to >= tracks.length) return false
        return tracks[to].kind === tracks[fromIndex].kind && !tracks[to].locked
    }
    function fitToWidth() {
        if (root.project.duration > 0 && lanes.width > 0)
            root.pxPerSecond = Math.max(2, Math.min(300, lanes.width / (root.project.duration * 1.04)))
    }
    function niceStep() {
        const steps = [0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600]
        for (let i = 0; i < steps.length; ++i)
            if (steps[i] * root.pxPerSecond >= 70) return steps[i]
        return 1200
    }
    function gridStep() {
        const major = root.niceStep()
        if (root.gridMode === "fine") return major / 4
        if (root.gridMode === "frames") {
            const fps = Math.max(1, root.project.frameRate)
            return 1 / fps
        }
        return major
    }
    function gridLineCount() {
        const step = root.gridStep()
        if (step <= 0) return 0
        return Math.ceil(root.timelineWidth / (step * root.pxPerSecond)) + 1
    }
    function isMajorGrid(t) {
        const major = root.niceStep()
        const n = Math.round(t / major)
        return Math.abs(t - n * major) < major * 0.02
    }
    function rulerLabel(t) {
        const minutes = Math.floor(t / 60)
        const seconds = t - minutes * 60
        const fine = root.niceStep() < 1
        const text = fine ? seconds.toFixed(1) : String(Math.floor(seconds + 1e-6))
        return minutes + ":" + (seconds < 10 ? "0" : "") + text
    }
    function zoomAround(factor, anchorX) {
        const t = (lanes.contentX + anchorX) / root.pxPerSecond
        root.pxPerSecond = Math.max(2, Math.min(300, root.pxPerSecond * factor))
        lanes.contentX = Math.max(0, t * root.pxPerSecond - anchorX)
    }

    Connections {
        target: root.project
        function onOpened() { Qt.callLater(root.fitToWidth) }
    }
    Component.onCompleted: Qt.callLater(fitToWidth)
    // Keep the playhead in view while playing: glide a page ahead.
    Connections {
        target: root.playback
        function onPositionChanged() {
            if (!root.playback.playing || root.scrubbing || followAnimation.running) return
            const x = root.playhead * root.pxPerSecond
            if (x > lanes.contentX + lanes.width * 0.92 || x < lanes.contentX) {
                followAnimation.to = Math.max(0, Math.min(lanes.contentWidth - lanes.width, x - lanes.width * 0.08))
                followAnimation.restart()
            }
        }
    }
    NumberAnimation {
        id: followAnimation
        target: lanes
        property: "contentX"
        duration: 260
        easing.type: Easing.OutCubic
    }
    Timer {  // edge auto-scroll while scrubbing or dragging clips
        interval: 16
        repeat: true
        running: root.edgeVelocity !== 0
        onTriggered: {
            const before = lanes.contentX
            root.scrollBy(root.edgeVelocity, 0)
            if (lanes.contentX === before) { root.edgeVelocity = 0; return }
            if (root.scrubbing) root.scrubTo(root.scrubSceneX, root.scrubMods)
            root.autoScrolled()
        }
    }

    // ---- Toolbar -------------------------------------------------------------
    RowLayout {
        id: toolbar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 40
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 6
        Label {
            text: root.project.formatTimecode(root.playhead)
            color: Theme.text
            font.family: Theme.monoFamily
            font.pixelSize: Theme.fontM
        }
        Label {
            readonly property int n: root.project.selectedClips.length
            text: n === 0 ? ""
                  : n === 1 ? "· " + (root.project.selection.name || "")
                  : "· " + n + " clips selected"
            color: Theme.textMuted
            font.pixelSize: Theme.fontXS
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: 260
        }
        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
        IconButton {
            iconName: "cut"
            iconSize: 15
            tooltip: "Split at playhead (S)"
            enabled: root.project.duration > 0
            onClicked: { root.activated(); root.project.splitAt(root.playhead) }
        }
        IconButton {
            iconName: "trash"
            iconSize: 15
            tooltip: root.cutAllTracks
                ? "Delete selected (⌫) — closes the gap on every track"
                : "Delete selected (⌫) — this track only, leaves a gap"
            enabled: root.project.selectedClips.length > 0 || root.project.selectedClip.length > 0
            onClicked: { root.activated(); root.project.deleteSelected() }
        }
        IconButton {
            iconName: "music"
            iconSize: 15
            tooltip: "Add audio at playhead"
            enabled: root.project.duration > 0 && !root.project.busy
            onClicked: { root.activated(); audioDialog.open() }
        }
        IconButton {
            iconName: "marker"
            iconSize: 15
            tooltip: "Add marker (M)"
            enabled: root.project.duration > 0
            onClicked: { root.activated(); root.project.addMarker(root.playhead, "") }
        }
        IconButton {
            objectName: "snapToggle"
            iconName: "magnet"
            iconSize: 15
            active: root.snapping
            tooltip: root.snapping ? "Snapping on — hold ⌘ to place freely" : "Snapping off"
            onClicked: root.snapping = !root.snapping
        }
        Rectangle { width: 1; height: 18; color: Theme.stroke }
        ColumnLayout {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter
            Label {
                text: "Select"
                color: Theme.textFaint
                font.pixelSize: 9
            }
            Segmented {
                implicitHeight: 26
                Layout.minimumWidth: implicitWidth
                options: [
                    { label: "Drag", value: "drag" },
                    { label: "⇧", value: "shift" },
                    { label: "Scrub", value: "scrub" }
                ]
                value: root.dragSelectMode
                onSelected: v => { if (root.editor) root.editor.dragSelectMode = v }
            }
        }
        ColumnLayout {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter
            Label {
                text: "Box"
                color: Theme.textFaint
                font.pixelSize: 9
            }
            Segmented {
                implicitHeight: 26
                Layout.minimumWidth: implicitWidth
                options: [
                    { label: "Touch", value: "touch" },
                    { label: "Inside", value: "inside" }
                ]
                value: root.marqueeMatch
                onSelected: v => { if (root.editor) root.editor.marqueeMatch = v }
            }
        }
        ColumnLayout {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter
            Label {
                text: "Cut"
                color: Theme.textFaint
                font.pixelSize: 9
            }
            Segmented {
                implicitHeight: 26
                Layout.minimumWidth: implicitWidth
                options: [
                    { label: "Track", value: "track" },
                    { label: "All", value: "allTracks" }
                ]
                value: root.linkedEditMode
                onSelected: v => { if (root.editor) root.editor.linkedEditMode = v }
            }
        }
        ColumnLayout {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter
            Label {
                text: "Move"
                color: Theme.textFaint
                font.pixelSize: 9
            }
            Segmented {
                objectName: "moveMode"
                implicitHeight: 26
                Layout.minimumWidth: implicitWidth
                options: [
                    { label: "Linked", value: "linked" },
                    { label: "One", value: "one" }
                ]
                value: root.moveMode
                onSelected: v => { if (root.editor) root.editor.timelineMoveMode = v }
            }
        }
        IconButton {
            iconName: "layout"
            iconSize: 15
            tooltip: "Fit the whole video"
            onClicked: root.fitToWidth()
        }
        ColumnLayout {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter
            Label { text: "Zoom"; color: Theme.textFaint; font.pixelSize: 9 }
            MiniSlider {
                id: zoom
                from: 2
                to: 300
                Layout.preferredWidth: 80
                Layout.minimumWidth: 56
                onMoved: root.zoomAround(value / root.pxPerSecond, lanes.width / 2)
                Binding {
                    target: zoom
                    property: "value"
                    value: root.pxPerSecond
                    when: !zoom.pressed
                    restoreMode: Binding.RestoreNone
                }
            }
        }
        ColumnLayout {
            spacing: 0
            Layout.alignment: Qt.AlignVCenter
            Label { text: "Tracks"; color: Theme.textFaint; font.pixelSize: 9 }
            MiniSlider {
                id: trackSize
                from: 32
                to: 88
                stepSize: 4
                Layout.preferredWidth: 80
                Layout.minimumWidth: 56
                onMoved: { if (root.editor) root.editor.timelineTrackHeight = value }
                Binding {
                    target: trackSize
                    property: "value"
                    value: root.trackHeight
                    when: !trackSize.pressed
                    restoreMode: Binding.RestoreNone
                }
            }
        }
        Segmented {
            implicitHeight: 26
            Layout.minimumWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter
            options: [
                { label: "Grid", value: "auto" },
                { label: "Fine", value: "fine" },
                { label: "Fr", value: "frames" }
            ]
            value: root.gridMode
            onSelected: v => { if (root.editor) root.editor.timelineGridMode = v }
        }
    }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: toolbar.bottom; height: 1; color: Theme.stroke }

    // ---- Ruler + layout lane (scroll horizontally with the lanes) ------------
    Item {
        id: rulerArea
        anchors.left: parent.left
        anchors.leftMargin: root.headerWidth + 1
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.topMargin: 1
        height: root.rulerHeight + root.layoutHeight
        clip: true
        WheelHandler {
            id: rulerWheel
            target: null
            onWheel: event => {
                root.wheelAnchor = lanes.mapFromItem(rulerArea, rulerWheel.point.position.x, 0).x
                root.wheel(event)
            }
        }

        Item {
            x: -lanes.contentX
            width: root.timelineWidth
            height: parent.height

            Rectangle {
                id: ruler
                width: parent.width
                height: root.rulerHeight
                color: Theme.surface
                Repeater {
                    model: Math.ceil(root.timelineWidth / (root.niceStep() * root.pxPerSecond)) + 1
                    delegate: Item {
                        required property int index
                        x: index * root.niceStep() * root.pxPerSecond
                        height: ruler.height
                        Rectangle { width: 1; height: 7; anchors.bottom: parent.bottom; color: Theme.strokeStrong }
                        Label { x: 4; y: 3; text: root.rulerLabel(index * root.niceStep()); color: Theme.textFaint; font.pixelSize: 10 }
                    }
                }
                // In/out range being marked for removal.
                Rectangle {
                    visible: root.editor && root.editor.markIn >= 0
                    x: (root.editor ? root.editor.markIn : 0) * root.pxPerSecond
                    width: Math.max(2, ((root.editor && root.editor.markOut > root.editor.markIn ? root.editor.markOut : root.playhead)
                                        - (root.editor ? root.editor.markIn : 0)) * root.pxPerSecond)
                    height: ruler.height
                    color: Qt.rgba(1, 0.3, 0.37, 0.25)
                    border.color: Theme.record
                    border.width: 1
                }
                Repeater {
                    model: root.project.markers
                    delegate: Rectangle {
                        required property var modelData
                        x: modelData.time * root.pxPerSecond - width / 2
                        y: ruler.height - 12
                        width: 10
                        height: 10
                        rotation: 45
                        color: modelData.color
                        z: 2
                        ToolTip.visible: markerHover.hovered
                        ToolTip.text: modelData.label + " · " + root.project.formatTime(modelData.time)
                        HoverHandler { id: markerHover }
                    }
                }
                MouseArea {
                    objectName: "timelineRuler"
                    anchors.fill: parent
                    preventStealing: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.IBeamCursor
                    onPressed: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            root.menuTime = root.timeAt(mouse.x)
                            rulerMenu.popup()
                            return
                        }
                        root.beginScrub(mapToItem(null, mouse.x, 0).x, mouse.modifiers)
                    }
                    onPositionChanged: mouse => { if (pressed && root.scrubbing) root.scrubTo(mapToItem(null, mouse.x, 0).x, mouse.modifiers) }
                    onReleased: root.endScrub()
                    onCanceled: root.endScrub()
                }
            }

            // Layout sections
            Item {
                y: root.rulerHeight
                width: parent.width
                height: root.layoutHeight
                Repeater {
                    model: root.project.layoutRegions
                    delegate: Rectangle {
                        id: region
                        required property var modelData
                        required property int index
                        x: modelData.start * root.pxPerSecond + 1
                        width: Math.max(2, modelData.duration * root.pxPerSecond - 2)
                        height: parent.height - 3
                        y: 1
                        radius: 0
                        color: index % 2 ? (Theme.dark ? "#1F2740" : "#DCE3FF") : (Theme.dark ? "#252F4D" : "#CDD7FF")
                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            verticalAlignment: Text.AlignVCenter
                            text: region.modelData.name
                            color: Theme.textMuted
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.activated()
                                root.playback.seek(region.modelData.start)
                                if (root.editor) root.editor.tool = "layout"
                            }
                        }
                    }
                }
            }

            Rectangle {  // playhead head (drag it, or anywhere on the ruler, to scrub)
                x: root.playhead * root.pxPerSecond - width / 2
                y: root.rulerHeight - height
                width: 13
                height: 15
                radius: 2
                color: root.scrubbing ? Theme.accent : Theme.text
                z: 5
            }
            Rectangle {  // time while scrubbing
                visible: root.scrubbing
                x: Math.max(lanes.contentX + 2, Math.min(lanes.contentX + lanes.width - width - 2,
                                                         root.playhead * root.pxPerSecond + 10))
                y: 2
                width: scrubTime.implicitWidth + 10
                height: root.rulerHeight - 6
                radius: 3
                color: Theme.accent
                z: 6
                Label {
                    id: scrubTime
                    anchors.centerIn: parent
                    text: root.project.formatTimecode(root.playhead)
                    color: Theme.accentText
                    font.family: Theme.monoFamily
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    // ---- Track headers (scroll vertically with the lanes) ---------------------
    Item {
        id: headerArea
        anchors.left: parent.left
        anchors.top: rulerArea.bottom
        anchors.bottom: parent.bottom
        width: root.headerWidth
        clip: true
        Label {
            visible: false
            text: "Layout"
        }
        Column {
            y: -lanes.contentY
            width: parent.width
            Repeater {
                model: root.project.tracks
                delegate: TrackHeader {
                    required property var modelData
                    width: root.headerWidth
                    height: root.trackHeight
                    track: modelData
                    project: root.project
                    onActivated: root.activated()
                }
            }
        }
    }
    // Above the headers: add a layer, move the selected one up/down.
    Row {
        id: layerTools
        anchors.left: parent.left
        anchors.leftMargin: 8
        y: rulerArea.y + 1
        height: root.rulerHeight - 2
        spacing: 2
        readonly property var selectedTrack: {
            const tracks = root.project.tracks
            for (let i = 0; i < tracks.length; ++i)
                if (tracks[i].id === root.project.selectedTrack) return tracks[i]
            return null
        }
        AbstractButton {
            id: addLayerButton
            objectName: "addLayerButton"
            height: parent.height
            width: addLayerRow.implicitWidth + 14
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            enabled: root.project.loaded
            onClicked: addLayerMenu.popup(addLayerButton, 0, addLayerButton.height)
            ToolTip.visible: hovered
            ToolTip.text: "Add a layer at the playhead: text, image or video, sound, caption"
            ToolTip.delay: 450
            background: Rectangle {
                radius: 3
                color: addLayerButton.down ? Theme.pressed : addLayerButton.hovered ? Theme.hover : Theme.accentSoft
            }
            contentItem: Row {
                id: addLayerRow
                spacing: 4
                leftPadding: 7
                Icon { name: "plus"; size: 12; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                Label {
                    text: "Layer"
                    color: Theme.accent
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        IconButton {
            objectName: "layerUpButton"
            width: 22
            height: parent.height
            iconName: "chevron-left"
            iconSize: 12
            rotation: 90
            enabled: !!layerTools.selectedTrack && layerTools.selectedTrack.canMoveUp
            tooltip: "Move the selected layer up"
            onClicked: { root.activated(); root.project.moveTrack(layerTools.selectedTrack.id, 1) }
        }
        IconButton {
            objectName: "layerDownButton"
            width: 22
            height: parent.height
            iconName: "chevron-left"
            iconSize: 12
            rotation: -90
            enabled: !!layerTools.selectedTrack && layerTools.selectedTrack.canMoveDown
            tooltip: "Move the selected layer down"
            onClicked: { root.activated(); root.project.moveTrack(layerTools.selectedTrack.id, -1) }
        }
    }
    Label {
        anchors.left: parent.left
        anchors.leftMargin: 12
        y: rulerArea.y + root.rulerHeight + 2
        text: "Layout"
        color: Theme.textFaint
        font.pixelSize: 10
    }
    Rectangle { anchors.left: headerArea.right; anchors.top: toolbar.bottom; anchors.bottom: parent.bottom; width: 1; color: Theme.stroke }

    // ---- Lanes --------------------------------------------------------------------
    Flickable {
        id: lanes
        objectName: "timelineLanes"
        anchors.left: headerArea.right
        anchors.leftMargin: 1
        anchors.right: parent.right
        anchors.top: rulerArea.bottom
        anchors.bottom: parent.bottom
        contentWidth: root.timelineWidth
        contentHeight: Math.max(height, root.project.tracks.length * root.trackHeight)
        clip: true
        // Mouse drags belong to clips, the playhead and box selection — never
        // to panning (it used to steal clip and trim drags). Scrolling is the
        // wheel/trackpad (below) and the scroll bars.
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        onWidthChanged: if (contentX > Math.max(0, contentWidth - width)) contentX = Math.max(0, contentWidth - width)

        WheelHandler {
            id: lanesWheel
            target: null
            onWheel: event => {
                // Handlers inside a Flickable live on its content item.
                root.wheelAnchor = lanesWheel.point.position.x - lanes.contentX
                root.wheel(event)
            }
        }
        PinchHandler {  // trackpad pinch zooms around the fingers
            target: null
            onScaleChanged: delta => root.zoomAround(delta, Math.max(0, Math.min(lanes.width, centroid.position.x - lanes.contentX)))
        }

        Column {
            id: trackLanes
            width: root.timelineWidth
            Repeater {
                model: root.project.tracks
                delegate: Rectangle {
                    id: lane
                    required property var modelData
                    required property int index
                    width: root.timelineWidth
                    height: root.trackHeight
                    // A lane holding a dragged clip paints above the others so the clip can cross them.
                    z: lane.dragging ? 5 : 0
                    property bool dragging: false
                    readonly property bool selectedLayer: root.project.selectedTrack === lane.modelData.id
                    color: lane.modelData.locked ? Theme.inset : index % 2 ? Theme.bg : Theme.surface
                    Rectangle {  // selected layer / drop target tint
                        anchors.fill: parent
                        color: root.dropLane === lane.index ? (root.dropOk ? Theme.accent : Theme.danger) : Theme.accent
                        opacity: root.dropLane === lane.index ? 0.14 : lane.selectedLayer ? 0.06 : 0
                    }
                    Repeater {
                        model: root.gridLineCount()
                        delegate: Rectangle {
                            required property int index
                            readonly property real t: index * root.gridStep()
                            x: t * root.pxPerSecond
                            width: 1
                            height: parent.height
                            color: root.isMajorGrid(t) ? Theme.strokeStrong : Theme.stroke
                            opacity: root.isMajorGrid(t) ? 0.55 : 0.28
                        }
                    }
                    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.stroke }
                    MouseArea {  // empty lane: click seeks · drag box-select (see toolbar options)
                        anchors.fill: parent
                        property real pressX: 0
                        property real pressY: 0
                        property bool dragSelect: false
                        property bool clickPending: false
                        preventStealing: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        property bool scrub: false
                        onPressed: mouse => {
                            root.activated()
                            if (mouse.button === Qt.RightButton) {
                                root.project.selectTrack(lane.modelData.id)
                                root.menuTime = root.timeAt(mouse.x)
                                laneMenu.popup()
                                clickPending = false
                                dragSelect = false
                                return
                            }
                            const p = mapToItem(trackLanes, mouse.x, mouse.y)
                            pressX = p.x
                            pressY = p.y
                            dragSelect = false
                            scrub = false
                            if (root.dragSelectMode === "scrub") {
                                clickPending = false
                                root.project.clearSelection()
                                root.project.selectTrack(lane.modelData.id)
                                scrub = true
                                root.beginScrub(mapToItem(null, mouse.x, 0).x, mouse.modifiers)
                                return
                            }
                            if (root.dragSelectMode === "shift" && (mouse.modifiers & Qt.ShiftModifier)) {
                                clickPending = false
                                dragSelect = true
                                root.beginMarquee(pressX, pressY)
                                return
                            }
                            clickPending = root.dragSelectMode === "drag"
                            if (!clickPending) {
                                root.project.clearSelection()
                                root.project.selectTrack(lane.modelData.id)
                                root.playback.seek(root.frameTime(root.timeAt(mouse.x)))
                            }
                        }
                        onPositionChanged: mouse => {
                            if (!pressed || (pressedButtons & Qt.RightButton)) return
                            if (scrub) { root.scrubTo(mapToItem(null, mouse.x, 0).x, mouse.modifiers); return }
                            const p = mapToItem(trackLanes, mouse.x, mouse.y)
                            if (clickPending) {
                                const dx = p.x - pressX
                                const dy = p.y - pressY
                                if (dx * dx + dy * dy < 36) return
                                clickPending = false
                                dragSelect = true
                                root.beginMarquee(pressX, pressY)
                            }
                            if (!dragSelect) return
                            root.updateMarquee(p.x, p.y)
                        }
                        onReleased: mouse => {
                            if (scrub) {
                                root.endScrub()
                            } else if (dragSelect) {
                                const additive = mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)
                                const subtract = mouse.modifiers & Qt.AltModifier
                                root.finishMarquee(additive, subtract)
                            } else if (clickPending) {
                                root.project.clearSelection()
                                root.project.selectTrack(lane.modelData.id)
                                root.playback.seek(root.frameTime(root.timeAt(mouse.x)))
                            }
                            dragSelect = false
                            clickPending = false
                            scrub = false
                        }
                        onCanceled: {
                            if (scrub) root.endScrub()
                            root.marqueeActive = false
                            dragSelect = false
                            clickPending = false
                            scrub = false
                        }
                    }
                    Repeater {
                        model: lane.modelData.clips
                        delegate: ClipItem {
                            id: clipItem
                            required property var modelData
                            objectName: "clip-" + modelData.id
                            clip: modelData
                            video: lane.modelData.kind === "video"
                            locked: lane.modelData.locked
                            // Bound to the selection property so the highlight follows every change.
                            selected: root.project.selectedClips.indexOf(modelData.id) >= 0
                            pxPerSecond: root.pxPerSecond
                            laneHeight: root.trackHeight
                            dragSpace: trackLanes
                            snapMove: root.snapMove
                            snapEdge: root.snapEdge
                            acceptsLane: offset => root.laneAccepts(lane.index, offset)
                            y: 5
                            height: lane.height - 10
                            onDraggingChanged: lane.dragging = dragging
                            onSelectRequested: additive => {
                                root.activated()
                                root.project.selectClip(modelData.id, additive)
                            }
                            onDragPointer: sceneX => root.edgeDrag(sceneX)
                            onContextRequested: (sx, sy) => {
                                root.activated()
                                root.menuClip = modelData
                                root.menuTrackKind = lane.modelData.kind
                                clipMenu.popup()
                            }
                            groupShift: root.groupDragGroup !== "" && modelData.linkGroup === root.groupDragGroup ? root.groupDragDx : 0
                            onGroupDrag: dx => { root.groupDragGroup = modelData.linkGroup; root.groupDragDx = dx }
                            onDragEnded: { root.stopEdgeDrag(); root.dropLane = -1; root.groupDragGroup = ""; root.groupDragDx = 0 }
                            onLaneHover: (offset, ok) => {
                                root.dropLane = offset === 0 ? -1 : lane.index + offset
                                root.dropOk = ok
                            }
                            onTrimRequested: (edge, seconds) => root.project.trimClip(modelData.id, edge, seconds)
                            linkedMove: root.moveMode === "linked"
                            onMoveRequested: (seconds, laneOffset, alone) => {
                                const trackId = laneOffset !== 0 ? root.project.tracks[lane.index + laneOffset].id : ""
                                if (alone)
                                    root.project.moveClipAlone(modelData.id, seconds, trackId)
                                else if (laneOffset !== 0)
                                    root.project.moveClipToTrack(modelData.id, trackId, seconds)
                                else
                                    root.project.moveClip(modelData.id, seconds)
                            }
                            onOpenRequested: {
                                if (!root.editor) return
                                if (modelData.role === "text") root.editor.tool = "style"
                                else if (modelData.role === "subtitle") root.editor.tool = "subtitles"
                                else if (modelData.role === "overlay") root.editor.tool = "overlay"
                                else if (lane.modelData.kind === "audio") root.editor.tool = "audio"
                                else root.editor.tool = "effects"
                            }
                            Connections {
                                target: root
                                function onAutoScrolled() { clipItem.updateDrag() }
                            }
                        }
                    }
                }
            }
        }

        // Detected pauses (Cut → Remove pauses).
        Repeater {
            model: root.project.silences
            delegate: Rectangle {
                required property var modelData
                x: modelData.start * root.pxPerSecond
                width: Math.max(2, modelData.duration * root.pxPerSecond)
                height: lanes.contentHeight
                color: Qt.rgba(1, 0.3, 0.37, 0.18)
                border.color: Qt.rgba(1, 0.3, 0.37, 0.5)
                border.width: 1
            }
        }
        // In/out range
        Rectangle {
            visible: root.editor && root.editor.markIn >= 0
            x: (root.editor ? root.editor.markIn : 0) * root.pxPerSecond
            width: Math.max(2, ((root.editor && root.editor.markOut > root.editor.markIn ? root.editor.markOut : root.playhead)
                                - (root.editor ? root.editor.markIn : 0)) * root.pxPerSecond)
            height: lanes.contentHeight
            color: Qt.rgba(1, 0.3, 0.37, 0.12)
        }
        // Marker guides
        Repeater {
            model: root.project.markers
            delegate: Rectangle {
                required property var modelData
                x: modelData.time * root.pxPerSecond
                width: 1
                height: lanes.contentHeight
                color: Qt.rgba(0.96, 0.65, 0.14, 0.35)
            }
        }
        // Shift-drag selection box (across tracks)
        Rectangle {
            visible: root.marqueeActive
            x: Math.min(root.marqueeX0, root.marqueeX1)
            y: Math.min(root.marqueeY0, root.marqueeY1)
            width: Math.max(1, Math.abs(root.marqueeX1 - root.marqueeX0))
            height: Math.max(1, Math.abs(root.marqueeY1 - root.marqueeY0))
            color: Qt.rgba(0.49, 0.55, 1, 0.12)
            border.width: 1
            border.color: Theme.accent
            z: 20
        }

        MouseArea {
            width: trackLanes.width
            height: trackLanes.height
            enabled: root.marqueeActive
            onPositionChanged: mouse => root.updateMarquee(mouse.x, mouse.y)
            onReleased: mouse => {
                const additive = mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier)
                const subtract = mouse.modifiers & Qt.AltModifier
                root.finishMarquee(additive, subtract)
            }
        }

        // Playhead: grab it anywhere along its line to scrub.
        Rectangle {
            x: root.playhead * root.pxPerSecond
            width: root.scrubbing || playheadGrab.containsMouse ? 2 : 1.5
            height: lanes.contentHeight
            color: root.scrubbing ? Theme.accent : Theme.text
            z: 10
        }
        MouseArea {
            id: playheadGrab
            objectName: "playheadGrab"
            x: root.playhead * root.pxPerSecond - width / 2
            width: 11
            height: lanes.contentHeight
            z: 11
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            onPressed: mouse => root.beginScrub(mapToItem(null, mouse.x, 0).x, mouse.modifiers)
            onPositionChanged: mouse => { if (pressed) root.scrubTo(mapToItem(null, mouse.x, 0).x, mouse.modifiers) }
            onReleased: root.endScrub()
            onCanceled: root.endScrub()
        }
    }
}
