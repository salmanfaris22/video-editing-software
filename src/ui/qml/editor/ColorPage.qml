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

        // ---- Clips and Primaries -------------------------------------------------
        Rectangle {
            SplitView.preferredHeight: 400
            SplitView.minimumHeight: 330
            color: Theme.surface

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Clips strip: click to grade that clip.
                ListView {
                    id: strip
                    objectName: "colorClips"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 78
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
                        width: 120
                        height: 64
                        radius: 3
                        color: current ? Theme.accentSoft : Theme.raised
                        border.width: current ? 2 : 1
                        border.color: current ? Theme.accent : Theme.stroke
                        Image {
                            anchors.fill: parent
                            anchors.margins: 2
                            anchors.bottomMargin: 18
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.height: 80
                            visible: (parent.modelData.media || "").length > 0
                            source: visible ? "image://thumbnail/" + encodeURIComponent(parent.modelData.media)
                                              + "?t=" + (parent.modelData.sourceIn + parent.modelData.duration / 2).toFixed(1) : ""
                        }
                        Label {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 4
                            text: (parent.index + 1) + " · " + parent.modelData.label + " · " + root.project.formatTime(parent.modelData.start)
                            color: parent.current ? Theme.text : Theme.textMuted
                            font.pixelSize: 10
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
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.stroke }

                // Palette header
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 6
                    Label {
                        text: "Primaries – Color Wheels"
                        color: Theme.text
                        font.pixelSize: Theme.fontM
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: root.gradable ? "· " + (root.sel.name || "") : "· select a clip to grade"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontS
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    PrimaryButton {
                        objectName: "resetGrade"
                        variant: "ghost"
                        text: "Reset grade"
                        enabled: root.gradable
                        onClicked: root.project.resetColor(root.clipId)
                    }
                }

                // Top row: Temp, Tint, Contrast, Pivot, Exposure
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    spacing: 14
                    enabled: root.gradable
                    opacity: enabled ? 1 : 0.4
                    ScrubField { objectName: "temp"; label: "Temp"; value: root.sel.temperature || 0; accent: "#F59E0B"; onEdited: v => root.setValue("temperature", v) }
                    ScrubField { objectName: "tint"; label: "Tint"; value: root.sel.tint || 0; accent: "#D946EF"; onEdited: v => root.setValue("tint", v) }
                    ScrubField { objectName: "contrast"; label: "Contrast"; value: root.sel.contrast || 0; defaultValue: 0; displayScale: 1; onEdited: v => root.setValue("contrast", v) }
                    ScrubField { objectName: "pivot"; label: "Pivot"; value: root.sel.pivot === undefined ? 0.5 : root.sel.pivot; from: 0; to: 1; defaultValue: 0.5; decimals: 3; onEdited: v => root.setValue("pivot", v) }
                    ScrubField { objectName: "exposure"; label: "Exposure"; value: root.sel.exposure || 0; from: -2; to: 2; onEdited: v => root.setValue("exposure", v) }
                }

                // The four wheels
                RowLayout {
                    id: wheels
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 6
                    spacing: 18
                    enabled: root.gradable
                    opacity: enabled ? 1 : 0.4
                    Repeater {
                        model: [
                            { wheel: "lift", title: "Lift", key: "lift" },
                            { wheel: "gamma", title: "Gamma", key: "gammaWheel" },
                            { wheel: "gain", title: "Gain", key: "gain" },
                            { wheel: "offset", title: "Offset", key: "offset" }
                        ]
                        delegate: ColorWheel {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.maximumWidth: 200
                            Layout.alignment: Qt.AlignTop
                            dialSize: Math.max(70, Math.min(width, wheels.height - 76))
                            project: root.project
                            wheel: modelData.wheel
                            title: modelData.title
                            value: root.sel[modelData.key] || ({ x: 0, y: 0, master: 0 })
                            onEdited: (x, y, m) => root.setWheel(modelData.wheel, x, y, m)
                        }
                    }
                }

                // Bottom row
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.bottomMargin: 10
                    spacing: 14
                    enabled: root.gradable
                    opacity: enabled ? 1 : 0.4
                    ScrubField { objectName: "colorBoost"; label: "Color Boost"; value: root.sel.colorBoost || 0; accent: "#22D3EE"; onEdited: v => root.setValue("colorBoost", v) }
                    ScrubField { objectName: "shadows"; label: "Shadows"; value: root.sel.shadows || 0; onEdited: v => root.setValue("shadows", v) }
                    ScrubField { objectName: "highlights"; label: "Highlights"; value: root.sel.highlights || 0; accent: Theme.text; onEdited: v => root.setValue("highlights", v) }
                    ScrubField { objectName: "saturation"; label: "Saturation"; value: root.sel.saturation || 0; accent: "#A78BFA"; onEdited: v => root.setValue("saturation", v) }
                    ScrubField { objectName: "hue"; label: "Hue"; value: root.sel.hue || 0; displayScale: 180; decimals: 0; accent: "#F472B6"; onEdited: v => root.setValue("hue", v) }
                }
            }
        }
    }
}
