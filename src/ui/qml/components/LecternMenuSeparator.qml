import QtQuick
import QtQuick.Controls.Basic

MenuSeparator {
    contentItem: Item {
        implicitHeight: 9
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            height: 1
            color: Theme.stroke
        }
    }
}
