import QtQuick
import QtQuick.Controls.Basic

TextField {
    implicitHeight: 34
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accent
    font.pixelSize: Theme.fontM
    leftPadding: 10
    rightPadding: 10
    background: Rectangle {
        radius: Theme.radiusM
        color: Theme.raised
        border.width: 1
        border.color: parent.activeFocus ? Theme.accent : Theme.stroke
    }
}
