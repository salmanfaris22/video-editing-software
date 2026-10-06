import QtQuick
import QtQuick.Controls.Basic

// Compact themed slider for toolbars: thin track, accent fill, small handle.
Slider {
    id: control

    implicitHeight: 18
    implicitWidth: 80
    focusPolicy: Qt.NoFocus
    hoverEnabled: true

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 3
        radius: 1.5
        color: Theme.strokeStrong
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 1.5
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 12
        height: 12
        radius: 6
        color: control.pressed ? Theme.accentHover : control.hovered ? "#FFFFFF" : Theme.text
        border.color: Theme.bg
        border.width: 1
    }
}
