import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Keyframes at the playhead: position, scale, rotation, anchor, opacity, crop.
ColumnLayout {
    id: root

    required property ProjectController project
    required property PlaybackController playback
    required property string clipId
    property bool keyframeMode: true
    property int _rev: 0

    Connections {
        target: root.project
        function onProjectChanged() { root._rev++ }
    }

    readonly property var tf: {
        root._rev
        root.playback.position
        if (!root.clipId.length) return ({})
        return root.project.clipTransformAt(root.clipId, root.playback.position)
    }
    readonly property bool onClip: root.tf.onClip === true

    function setProp(prop, value) {
        if (!root.clipId.length) return
        root.project.setClipKeyframe(root.clipId, prop, root.playback.position, value, root.keyframeMode && root.onClip)
    }

    SectionLabel { text: "Keyframes" }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ToggleRow {
            Layout.fillWidth: true
            iconName: "marker"
            title: "Keyframe at playhead"
            checked: root.keyframeMode
            onToggled: checked => root.keyframeMode = checked
        }
        PrimaryButton {
            variant: "ghost"
            text: "Clear all"
            enabled: root.clipId.length > 0
            onClicked: root.project.clearClipKeyframes(root.clipId, "all")
        }
    }
    Label {
        Layout.fillWidth: true
        visible: root.clipId.length > 0 && !root.onClip
        text: "Move the playhead onto this clip to add keys."
        color: Theme.warning
        font.pixelSize: Theme.fontXS
    }

    SliderRow {
        label: "Position ↔"
        from: 0; to: 1; defaultValue: 0.5
        value: root.tf.x !== undefined ? root.tf.x : 0.5
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("positionx", v)
    }
    SliderRow {
        label: "Position ↕"
        from: 0; to: 1; defaultValue: 0.5
        value: root.tf.y !== undefined ? root.tf.y : 0.5
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("positiony", v)
    }
    SliderRow {
        label: "Scale"
        from: 0.05; to: 2; defaultValue: 0.3
        value: root.tf.scale !== undefined ? root.tf.scale : 0.3
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("scale", v)
    }
    SliderRow {
        label: "Rotation"
        from: -180; to: 180; defaultValue: 0
        value: root.tf.rotation !== undefined ? root.tf.rotation : 0
        format: v => Math.round(v) + "°"
        onMoved: v => root.setProp("rotation", v)
    }
    SliderRow {
        label: "Anchor ↔"
        from: 0; to: 1; defaultValue: 0.5
        value: root.tf.anchorX !== undefined ? root.tf.anchorX : 0.5
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("anchorx", v)
    }
    SliderRow {
        label: "Anchor ↕"
        from: 0; to: 1; defaultValue: 0.5
        value: root.tf.anchorY !== undefined ? root.tf.anchorY : 0.5
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("anchory", v)
    }
    SliderRow {
        label: "Opacity"
        from: 0; to: 1; defaultValue: 1
        value: root.tf.opacity !== undefined ? root.tf.opacity : 1
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("opacity", v)
    }
    SectionLabel { text: "Crop (source edges)" }
    SliderRow {
        label: "Left"
        from: 0; to: 0.4; defaultValue: 0
        value: root.tf.cropL !== undefined ? root.tf.cropL : 0
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("cropl", v)
    }
    SliderRow {
        label: "Top"
        from: 0; to: 0.4; defaultValue: 0
        value: root.tf.cropT !== undefined ? root.tf.cropT : 0
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("cropt", v)
    }
    SliderRow {
        label: "Right"
        from: 0; to: 0.4; defaultValue: 0
        value: root.tf.cropR !== undefined ? root.tf.cropR : 0
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("cropr", v)
    }
    SliderRow {
        label: "Bottom"
        from: 0; to: 0.4; defaultValue: 0
        value: root.tf.cropB !== undefined ? root.tf.cropB : 0
        format: v => Math.round(v * 100) + "%"
        onMoved: v => root.setProp("cropb", v)
    }
}
