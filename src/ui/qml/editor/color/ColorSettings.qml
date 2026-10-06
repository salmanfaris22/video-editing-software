import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Color page's settings panel: the Color Space Transform of the clip
// (how its source is read: color space, camera log) and the film and lens
// effects applied after the grade — Film Emulation, Glow, Halation, Film
// Grain, Vignette, Lens Blur and Background Blur.
Rectangle {
    id: root
    objectName: "colorSettings"

    property ProjectController project
    property string clipId
    property var sel: ({})

    readonly property bool gradable: root.clipId.length > 0 && root.sel.visual === true && root.sel.role !== "text"
    readonly property var spaces: [
        { id: "auto", name: "Auto (file)" }, { id: "rec709", name: "Rec.709" }, { id: "srgb", name: "sRGB" },
        { id: "display-p3", name: "Display P3" }, { id: "rec2020", name: "Rec.2020" },
        { id: "rec2020-hlg", name: "Rec.2020 HLG (HDR)" }, { id: "rec2020-pq", name: "Rec.2020 PQ (HDR)" }
    ]
    readonly property var logs: {
        const out = [{ id: "", name: "None" }]
        for (const l of root.project.builtinLuts) out.push({ id: l.id, name: l.name.replace(" → Rec.709", "") })
        return out
    }

    color: "#121419"

    function on(type, enabled) { root.project.setEffectEnabled(root.clipId, type, enabled) }
    function set(type, param, v) { root.project.setEffectValue(root.clipId, type, param, v) }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 20
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            x: 10
            y: 8
            width: parent.width - 20
            spacing: 8
            enabled: root.gradable
            opacity: enabled ? 1 : 0.45

            Label { text: "Color Space Transform"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 6
                Label { text: "Input color space"; color: Theme.textMuted; font.pixelSize: 11 }
                DarkCombo {
                    objectName: "inputSpace"
                    Layout.fillWidth: true
                    implicitHeight: 26
                    font.pixelSize: 11
                    model: root.spaces.map(s => s.name)
                    currentIndex: Math.max(0, root.spaces.findIndex(s => s.id === (root.sel.inputColorSpace || "auto")))
                    onActivated: index => root.project.setInputColorSpace(root.clipId, root.spaces[index].id)
                }
                Label { text: "Camera log"; color: Theme.textMuted; font.pixelSize: 11 }
                DarkCombo {
                    objectName: "cameraLog"
                    Layout.fillWidth: true
                    implicitHeight: 26
                    font.pixelSize: 11
                    model: root.logs.map(s => s.name)
                    currentIndex: Math.max(0, root.logs.findIndex(s => s.id === (root.sel.lut || "")))
                    onActivated: index => root.project.setColorLut(root.clipId, root.logs[index].id)
                }
                Label { text: "Output"; color: Theme.textMuted; font.pixelSize: 11 }
                Label { text: "Rec.709 · Gamma 2.4"; color: Theme.text; font.pixelSize: 11 }
            }
            Label {
                Layout.fillWidth: true
                visible: (root.sel.detectedColorSpace || "").length > 0
                text: "File: " + (root.sel.detectedColorSpace || "")
                color: Theme.textFaint
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#23262E"; Layout.topMargin: 4 }
            Label { text: "Effects"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }

            EffectRow {
                objectName: "fx-film"
                title: "Film Emulation"
                type: "film-emulation"
                onSwitched: on => root.on(type, on)
                checked: root.sel.filmOn === true
                DarkCombo {
                    Layout.fillWidth: true
                    implicitHeight: 24
                    font.pixelSize: 11
                    model: root.project.filmStocks.map(s => s.name)
                    currentIndex: Math.round(root.sel.filmStock || 0)
                    onActivated: index => root.set("film-emulation", "stock", index)
                }
                FxSlider { label: "Amount"; value: root.sel.filmAmount; onMoved: v => root.set("film-emulation", "amount", v) }
            }
            EffectRow {
                objectName: "fx-glow"
                title: "Glow"
                type: "glow"
                onSwitched: on => root.on(type, on)
                checked: root.sel.glowOn === true
                FxSlider { label: "Amount"; value: root.sel.glow; onMoved: v => root.set("glow", "amount", v) }
                FxSlider { label: "Threshold"; value: root.sel.glowThreshold; to: 0.98; onMoved: v => root.set("glow", "threshold", v) }
                FxSlider { label: "Spread"; value: root.sel.glowRadius; onMoved: v => root.set("glow", "radius", v) }
            }
            EffectRow {
                objectName: "fx-halation"
                title: "Halation"
                type: "halation"
                onSwitched: on => root.on(type, on)
                checked: root.sel.halationOn === true
                FxSlider { label: "Amount"; value: root.sel.halation; onMoved: v => root.set("halation", "amount", v) }
                FxSlider { label: "Threshold"; value: root.sel.halationThreshold; to: 0.98; onMoved: v => root.set("halation", "threshold", v) }
                FxSlider { label: "Spread"; value: root.sel.halationRadius; onMoved: v => root.set("halation", "radius", v) }
            }
            EffectRow {
                objectName: "fx-grain"
                title: "Film Grain"
                type: "film-grain"
                onSwitched: on => root.on(type, on)
                checked: root.sel.grainOn === true
                FxSlider { label: "Amount"; value: root.sel.grain; onMoved: v => root.set("film-grain", "amount", v) }
                FxSlider { label: "Size"; value: root.sel.grainSize; from: 0.5; to: 4; onMoved: v => root.set("film-grain", "size", v) }
            }
            EffectRow {
                objectName: "fx-vignette"
                title: "Vignette"
                type: "vignette"
                onSwitched: on => root.on(type, on)
                checked: root.sel.vignetteOn === true
                FxSlider { label: "Amount"; value: root.sel.vignette; onMoved: v => root.set("vignette", "amount", v) }
            }
            EffectRow {
                objectName: "fx-blur"
                title: "Lens Blur"
                type: "blur"
                onSwitched: on => root.on(type, on)
                checked: root.sel.blurOn === true
                FxSlider { label: "Amount"; value: root.sel.blur; onMoved: v => root.set("blur", "amount", v) }
            }
            EffectRow {
                objectName: "fx-bgblur"
                title: "Background Blur"
                type: "background-blur"
                onSwitched: on => root.on(type, on)
                checked: root.sel.backgroundBlurOn === true
                visible: root.project.segmentationAvailable
                FxSlider { label: "Amount"; value: root.sel.backgroundBlur; onMoved: v => root.set("background-blur", "amount", v) }
            }
        }
    }

    // One effect: a switch row; its controls show while it is on.
    component EffectRow: ColumnLayout {
        id: row
        property string title
        property string type
        property bool checked: false
        default property alias controls: body.data
        signal switched(bool on)
        Layout.fillWidth: true
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: row.title; color: row.checked ? Theme.text : "#C3C7D0"; font.pixelSize: 11 }
            Switch {
                id: toggle
                objectName: row.objectName + "-switch"
                checked: row.checked
                implicitHeight: 20
                padding: 0
                onToggled: row.switched(checked)
                indicator: Rectangle {
                    implicitWidth: 30
                    implicitHeight: 16
                    radius: 8
                    color: toggle.checked ? Theme.accent : "#2A2E37"
                    border.color: toggle.checked ? Theme.accent : "#3A3F4B"
                    Rectangle {
                        x: toggle.checked ? parent.width - width - 2 : 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        radius: 6
                        color: "#F2F3F6"
                        Behavior on x { NumberAnimation { duration: 90 } }
                    }
                }
            }
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.leftMargin: 6
            visible: row.checked
            spacing: 2
        }
    }

    component FxSlider: RowLayout {
        id: fx
        property string label
        property var value: 0
        property real from: 0
        property real to: 1
        signal moved(real v)
        Layout.fillWidth: true
        spacing: 6
        Label { text: fx.label; color: Theme.textMuted; font.pixelSize: 10; Layout.preferredWidth: 58 }
        MiniSlider {
            Layout.fillWidth: true
            from: fx.from
            to: fx.to
            value: Number(fx.value || 0)
            onMoved: fx.moved(value)
        }
        Label {
            text: Number(fx.value || 0).toFixed(2)
            color: Theme.textFaint
            font.pixelSize: 10
            font.family: Theme.monoFamily
            Layout.preferredWidth: 30
        }
    }
}
