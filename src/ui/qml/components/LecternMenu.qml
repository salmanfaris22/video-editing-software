import QtQuick
import QtQuick.Controls.Basic

// Context menus: flat dark panel, no heavy white border (Basic style default).
Menu {
    id: control

    property bool compact: false

    topPadding: 4
    bottomPadding: 4
    spacing: 0

    background: Rectangle {
        implicitWidth: control.compact ? 48 : 240
        color: Theme.raised
        border.width: 1
        border.color: Theme.strokeStrong
        radius: 0
    }

}
