import QtQuick

// Segmented audio level meter. `level` is 0..1 (−60 … 0 dBFS).
Item {
    id: root

    property real level: 0
    property bool clipping: false
    property int segments: 24
    property bool active: true

    implicitHeight: 8
    implicitWidth: 160

    // Fast attack, slower release, like a hardware meter.
    property real shown: 0
    onLevelChanged: shown = level > shown ? level : shown * 0.82 + level * 0.18
    Behavior on shown { NumberAnimation { duration: 60 } }

    Row {
        anchors.fill: parent
        spacing: 2
        Repeater {
            model: root.segments
            delegate: Rectangle {
                required property int index
                readonly property real threshold: (index + 1) / root.segments
                width: (root.width - (root.segments - 1) * 2) / root.segments
                height: root.height
                radius: 1.5
                color: {
                    if (!root.active || root.shown < threshold - 0.5 / root.segments)
                        return Theme.stroke
                    if (threshold > 0.92) return root.clipping ? Theme.danger : "#F06B5B"
                    if (threshold > 0.78) return Theme.warning
                    return Theme.success
                }
            }
        }
    }
}
