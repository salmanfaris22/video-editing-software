import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A labelled slider. `moved` fires while dragging (the controller merges a
// drag into one undo step); double-click the label to reset to `defaultValue`.
ColumnLayout {
    id: root

    property string label
    property real value: 0
    property real from: 0
    property real to: 1
    property real stepSize: 0
    property real defaultValue: from
    property int decimals: 2
    property string suffix
    property var format: null  // optional function(value) → string
    signal moved(real value)

    spacing: 2
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: root.label
            color: root.enabled ? Theme.textMuted : Theme.textFaint
            font.pixelSize: Theme.fontS
            elide: Text.ElideRight
            MouseArea {
                anchors.fill: parent
                onDoubleClicked: root.moved(root.defaultValue)
                ToolTip.visible: containsMouse
                ToolTip.delay: 900
                ToolTip.text: "Double-click to reset"
                hoverEnabled: true
            }
        }
        Label {
            text: root.format ? root.format(slider.value) : slider.value.toFixed(root.decimals) + root.suffix
            color: root.enabled ? Theme.text : Theme.textFaint
            font.pixelSize: Theme.fontS
            font.family: Theme.monoFamily
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        from: root.from
        to: root.to
        stepSize: root.stepSize
        enabled: root.enabled
        focusPolicy: Qt.NoFocus
        onMoved: root.moved(value)

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: 2
            color: Theme.stroke
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: slider.enabled ? Theme.accent : Theme.strokeStrong
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: slider.pressed ? Theme.accentHover : "#FFFFFF"
            border.width: 1
            border.color: Theme.strokeStrong
        }
    }

    // Follow the model (undo, other views) except while the user drags.
    Binding {
        target: slider
        property: "value"
        value: root.value
        when: !slider.pressed
        restoreMode: Binding.RestoreNone
    }
}
