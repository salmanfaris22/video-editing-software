import QtQuick
import QtQuick.Controls.Basic

// Compact segmented control. `options`: [{label, value}].
Rectangle {
    id: root

    property var options: []
    property var value
    signal selected(var value)

    implicitHeight: 34
    // Natural width from the labels (equal segments). It used to come from the
    // segments, whose widths come from the row: a loop that collapsed the
    // control to nothing inside a RowLayout.
    implicitWidth: {
        let widest = 0
        for (let i = 0; i < root.options.length; ++i)
            widest = Math.max(widest, metrics.advanceWidth(String(root.options[i].label)))
        return 6 + root.options.length * (Math.ceil(widest) + 16) + Math.max(0, root.options.length - 1) * row.spacing
    }
    FontMetrics {
        id: metrics
        font.pixelSize: Theme.fontS
        font.weight: Font.DemiBold
    }
    radius: Theme.radiusM
    color: Theme.surface
    border.width: 1
    border.color: Theme.stroke
    opacity: enabled ? 1 : 0.5

    Row {
        id: row
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2
        Repeater {
            model: root.options
            delegate: AbstractButton {
                id: segment
                required property var modelData
                focusPolicy: Qt.NoFocus
                readonly property bool checkedState: root.value === modelData.value
                width: (row.width - (root.options.length - 1) * row.spacing) / root.options.length
                height: row.height
                hoverEnabled: true
                enabled: root.enabled
                onClicked: root.selected(modelData.value)
                background: Rectangle {
                    radius: Theme.radiusS
                    color: segment.checkedState ? Theme.accentSoft : segment.hovered ? Theme.hover : "transparent"
                    border.width: segment.checkedState ? 1 : 0
                    border.color: Qt.rgba(0.49, 0.55, 1.0, 0.45)
                }
                contentItem: Label {
                    text: segment.modelData.label
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: Theme.fontS
                    font.weight: segment.checkedState ? Font.DemiBold : Font.Normal
                    color: segment.checkedState ? Theme.text : Theme.textMuted
                }
            }
        }
    }
}
