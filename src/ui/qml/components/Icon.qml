import QtQuick

// Monochrome SVG icon, recolored by the C++ "icon" image provider (no
// shader effect pass; works with every scene-graph backend).
Item {
    id: root

    property string name
    property real size: 18
    property color color: "white"

    readonly property string hex: {
        const s = String(root.color)
        return (s.length === 9 ? s.substring(3) : s.substring(1)).toUpperCase()  // #AARRGGBB or #RRGGBB
    }

    implicitWidth: size
    implicitHeight: size

    Image {
        anchors.fill: parent
        source: root.name.length > 0 ? "image://icon/" + root.name + "/" + root.hex : ""
        sourceSize: Qt.size(Math.ceil(root.size * 2), Math.ceil(root.size * 2))
        smooth: true
        cache: true
    }
}
