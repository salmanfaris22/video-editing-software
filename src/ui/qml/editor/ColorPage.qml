import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Color page (DaVinci Resolve style): viewer and scopes on top, a strip of
// the clips to grade, and the Primaries palette — Lift / Gamma / Gain /
// Offset wheels with Temp, Tint, Contrast, Pivot above and Color Boost,
// Shadows, Highlights, Saturation, Hue below. Every control is a regular
// undoable project edit (slider drags merge into one step).
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

    /// Picks the clip to grade when nothing gradable is selected: the screen (or camera) at the playhead.
    function ensureSelection() {
        if (root.gradable) return
        const t = root.playback.position
        const id = root.project.roleClipAt("screen", t) || root.project.roleClipAt("camera", t)
        if (id) root.project.selectClip(id)
        else if (root.clips.length > 0) root.project.selectClip(root.clips[0].id)
    }
    onVisibleChanged: if (visible) Qt.callLater(ensureSelection)
    Component.onCompleted: Qt.callLater(ensureSelection)

    property string colorPalette: root.editor && root.editor.colorPalette ? root.editor.colorPalette : "primaries"
    onColorPaletteChanged: if (root.editor) root.editor.colorPalette = colorPalette
    property string curveChannel: "y"
    readonly property string curveKey: "curve" + root.curveChannel.toUpperCase()
    readonly property var curveChannels: [
        { id: "y", color: "#E6E8EE" },
        { id: "r", color: "#F05252" },
        { id: "g", color: "#3FCB6A" },
        { id: "b", color: "#4D8DFF" }
    ]
    readonly property var scopeStats: scope.stats
    function resetCurves() { for (const c of ["y", "r", "g", "b"]) root.project.setCurve(root.clipId, c, []) }

    function setValue(key, v) { if (root.gradable) root.project.setColorValue(root.clipId, key, v) }
    function setWheel(name, x, y, m) { if (root.gradable) root.project.setColorWheel(root.clipId, name, x, y, m) }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: Rectangle {
            implicitHeight: 5
            color: SplitHandle.pressed ? Theme.accent : SplitHandle.hovered ? Theme.strokeStrong : Theme.stroke
        }

        // ---- Viewer and scopes ---------------------------------------------------
        Item {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 200
            RowLayout {
                anchors.fill: parent
                spacing: 0
                PreviewPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    project: root.project
                    playback: root.playback
                    onActivated: root.activated()
                }
                Rectangle { Layout.fillHeight: true; width: 1; color: Theme.stroke }
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: false
                    Layout.preferredWidth: Math.min(460, root.width * 0.34)
                    Layout.maximumWidth: Math.min(460, root.width * 0.34)
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 8
                        Label { text: "Scopes"; color: Theme.text; font.pixelSize: Theme.fontM; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        Segmented {
                            objectName: "scopeMode"
                            implicitHeight: 24
                            Layout.minimumWidth: implicitWidth
                            options: [
                                { label: "Parade", value: "parade" },
                                { label: "Wave", value: "waveform" },
                                { label: "Vector", value: "vectorscope" },
                                { label: "Hist", value: "histogram" }
                            ]
                            value: scope.mode
                            onSelected: v => {
                                scope.mode = v
                                if (root.editor) root.editor.scopeMode = v
                            }
                        }
                    }
                    ScopeItem {
                        id: scope
                        objectName: "scope"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        playback: root.playback
                        mode: root.editor && root.editor.scopeMode ? root.editor.scopeMode : "parade"
                    }
                }
            }
        }

        // ---- Clips and palettes ---------------------------------------------------
        Rectangle {
            SplitView.preferredHeight: 430
            SplitView.minimumHeight: 340
            color: "#121419"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Clips strip: click to grade that clip.
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
                            onClicked: {
                                root.activated()
                                root.project.selectClip(parent.modelData.id)
                                const p = root.playback.position
                                const c = parent.modelData
                                if (p < c.start || p >= c.start + c.duration) root.playback.seek(c.start + Math.min(0.5, c.duration / 2))
                            }
                        }
                    }
                }

                // Palette bar (like Resolve's): which palette, auto balance, reset.
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
                        Repeater {
                            model: [
                                { id: "primaries", label: "Primaries", icon: "adjust" },
                                { id: "curves", label: "Curves", icon: "effects" }
                            ]
                            delegate: AbstractButton {
                                id: tab
                                required property var modelData
                                objectName: "palette-" + modelData.id
                                readonly property bool current: root.colorPalette === modelData.id
                                Layout.preferredHeight: 28
                                Layout.preferredWidth: tabRow.implicitWidth + 22
                                hoverEnabled: true
                                focusPolicy: Qt.NoFocus
                                onClicked: root.colorPalette = modelData.id
                                background: Rectangle {
                                    radius: 3
                                    color: tab.current ? "#262A33" : tab.hovered ? "#1E2128" : "transparent"
                                    Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 2; color: Theme.record; visible: tab.current }
                                }
                                contentItem: Row {
                                    id: tabRow
                                    spacing: 6
                                    leftPadding: 11
                                    Icon { anchors.verticalCenter: parent.verticalCenter; name: tab.modelData.icon; size: 13; color: tab.current ? Theme.text : Theme.textMuted }
                                    Label { anchors.verticalCenter: parent.verticalCenter; text: tab.modelData.label; color: tab.current ? Theme.text : Theme.textMuted; font.pixelSize: 11; font.weight: tab.current ? Font.DemiBold : Font.Normal }
                                }
                            }
                        }
                        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: "#2A2E37"; Layout.leftMargin: 6; Layout.rightMargin: 6 }
                        Label {
                            Layout.fillWidth: true
                            text: (root.colorPalette === "curves" ? "Curves – Custom" : "Primaries – Color Wheels")
                                  + (root.gradable ? "   ·   " + (root.sel.name || "") : "   ·   select a clip to grade")
                            color: "#B8BDC8"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        AbstractButton {
                            id: autoButton
                            objectName: "autoBalance"
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 22
                            enabled: root.gradable && !!scopeStats.meanR
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            readonly property var scopeStats: root.scopeStats
                            onClicked: root.project.autoBalance(root.clipId, root.scopeStats.meanR, root.scopeStats.meanG, root.scopeStats.meanB)
                            ToolTip.visible: hovered
                            ToolTip.text: "Auto Balance — remove the color cast"
                            ToolTip.delay: 400
                            background: Rectangle { radius: 11; color: autoButton.down ? Theme.pressed : autoButton.hovered ? Theme.hover : "#1E2128"; border.color: "#363B46" }
                            contentItem: Label { text: "A"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; color: autoButton.enabled ? Theme.text : Theme.textFaint; font.pixelSize: 11; font.weight: Font.Bold }
                        }
                        AbstractButton {
                            id: resetButton
                            objectName: "resetGrade"
                            Layout.preferredHeight: 22
                            Layout.preferredWidth: resetLabel.implicitWidth + 20
                            enabled: root.gradable
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            onClicked: root.colorPalette === "curves" ? root.resetCurves() : root.project.resetColor(root.clipId)
                            background: Rectangle { radius: 3; color: resetButton.down ? Theme.pressed : resetButton.hovered ? Theme.hover : "#1E2128"; border.color: "#363B46" }
                            contentItem: Label { id: resetLabel; text: root.colorPalette === "curves" ? "Reset curves" : "Reset grade"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; color: resetButton.enabled ? Theme.text : Theme.textFaint; font.pixelSize: 11 }
                        }
                    }
                }

                // ---- Primaries – Color Wheels ----
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.colorPalette === "primaries"
                    spacing: 0
                    enabled: root.gradable
                    opacity: enabled ? 1 : 0.45

                    RowLayout {  // Temp · Tint · Contrast · Pivot · Exposure
                        Layout.fillWidth: true
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        Layout.topMargin: 8
                        spacing: 16
                        ScrubField { objectName: "temp"; label: "Temp"; value: root.sel.temperature || 0; accent: "#F59E0B"; onEdited: v => root.setValue("temperature", v) }
                        ScrubField { objectName: "tint"; label: "Tint"; value: root.sel.tint || 0; accent: "#D946EF"; onEdited: v => root.setValue("tint", v) }
                        ScrubField { objectName: "contrast"; label: "Contrast"; value: root.sel.contrast || 0; onEdited: v => root.setValue("contrast", v) }
                        ScrubField { objectName: "pivot"; label: "Pivot"; value: root.sel.pivot === undefined ? 0.5 : root.sel.pivot; from: 0; to: 1; defaultValue: 0.5; decimals: 3; onEdited: v => root.setValue("pivot", v) }
                        ScrubField { objectName: "exposure"; label: "Exposure"; value: root.sel.exposure || 0; from: -2; to: 2; onEdited: v => root.setValue("exposure", v) }
                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {  // the four wheels share the width
                        id: wheels
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        Layout.topMargin: 6
                        spacing: 0
                        Repeater {
                            model: [
                                { wheel: "lift", title: "Lift", key: "lift" },
                                { wheel: "gamma", title: "Gamma", key: "gammaWheel" },
                                { wheel: "gain", title: "Gain", key: "gain" },
                                { wheel: "offset", title: "Offset", key: "offset" }
                            ]
                            delegate: Item {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                ColorWheel {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.top: parent.top
                                    width: parent.width
                                    dialSize: Math.max(80, Math.min(parent.width - 36, parent.height - 78, 220))
                                    project: root.project
                                    wheel: parent.modelData.wheel
                                    title: parent.modelData.title
                                    value: root.sel[parent.modelData.key] || ({ x: 0, y: 0, master: 0 })
                                    onEdited: (x, y, m) => root.setWheel(parent.modelData.wheel, x, y, m)
                                }
                            }
                        }
                    }

                    RowLayout {  // Color Boost · Shadows · Highlights · Saturation · Hue
                        Layout.fillWidth: true
                        Layout.leftMargin: 14
                        Layout.rightMargin: 14
                        Layout.bottomMargin: 8
                        spacing: 16
                        ScrubField { objectName: "colorBoost"; label: "Color Boost"; value: root.sel.colorBoost || 0; accent: "#22D3EE"; onEdited: v => root.setValue("colorBoost", v) }
                        ScrubField { objectName: "shadows"; label: "Shadows"; value: root.sel.shadows || 0; onEdited: v => root.setValue("shadows", v) }
                        ScrubField { objectName: "highlights"; label: "Highlights"; value: root.sel.highlights || 0; accent: Theme.text; onEdited: v => root.setValue("highlights", v) }
                        ScrubField { objectName: "saturation"; label: "Saturation"; value: root.sel.saturation || 0; accent: "#A78BFA"; onEdited: v => root.setValue("saturation", v) }
                        ScrubField { objectName: "hue"; label: "Hue"; value: root.sel.hue || 0; displayScale: 180; decimals: 0; accent: "#F472B6"; onEdited: v => root.setValue("hue", v) }
                        Item { Layout.fillWidth: true }
                    }
                }

                // ---- Curves – Custom ----
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 10
                    visible: root.colorPalette === "curves"
                    spacing: 12
                    enabled: root.gradable
                    opacity: enabled ? 1 : 0.45
                    CurveEditor {
                        objectName: "curveEditor"
                        Layout.fillHeight: true
                        Layout.preferredWidth: Math.min(parent.width * 0.6, height * 1.6)
                        project: root.project
                        points: root.sel[root.curveKey] || []
                        tint: root.curveChannels.find(c => c.id === root.curveChannel).color
                        onEdited: pts => root.project.setCurve(root.clipId, root.curveChannel, pts)
                    }
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        spacing: 8
                        Label { text: "Channel"; color: Theme.textMuted; font.pixelSize: 11 }
                        Row {
                            spacing: 4
                            Repeater {
                                model: root.curveChannels
                                delegate: AbstractButton {
                                    id: chan
                                    required property var modelData
                                    objectName: "curve-" + modelData.id
                                    readonly property bool current: root.curveChannel === modelData.id
                                    readonly property bool edited: (root.sel["curve" + modelData.id.toUpperCase()] || []).length >= 2
                                    width: 34
                                    height: 28
                                    hoverEnabled: true
                                    focusPolicy: Qt.NoFocus
                                    onClicked: root.curveChannel = modelData.id
                                    background: Rectangle {
                                        radius: 3
                                        color: chan.current ? "#262A33" : chan.hovered ? "#1E2128" : "#16181D"
                                        border.color: chan.current ? chan.modelData.color : "#2A2E37"
                                    }
                                    contentItem: Item {
                                        Label { anchors.centerIn: parent; text: chan.modelData.id.toUpperCase(); color: chan.modelData.color; font.pixelSize: 12; font.weight: Font.Bold }
                                        Rectangle { visible: chan.edited; width: 5; height: 5; radius: 2.5; color: chan.modelData.color; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 4 }
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.preferredWidth: 220
                            wrapMode: Text.WordWrap
                            text: "Click the curve to add a point and drag it. Double-click or right-click a point to remove it. ⇧ for fine moves. Y (luma) applies to all channels before R, G, B."
                            color: Theme.textFaint
                            font.pixelSize: 10
                        }
                        AbstractButton {
                            objectName: "resetChannel"
                            implicitWidth: resetChannelLabel.implicitWidth + 20
                            implicitHeight: 24
                            hoverEnabled: true
                            focusPolicy: Qt.NoFocus
                            onClicked: root.project.setCurve(root.clipId, root.curveChannel, [])
                            background: Rectangle { radius: 3; color: parent.hovered ? Theme.hover : "#1E2128"; border.color: "#363B46" }
                            contentItem: Label { id: resetChannelLabel; text: "Reset " + root.curveChannel.toUpperCase() + " curve"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; color: Theme.text; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
    }
}
