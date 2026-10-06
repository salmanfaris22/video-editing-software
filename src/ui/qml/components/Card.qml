import QtQuick

Rectangle {
    id: root

    default property alias content: inner.data
    property int padding: Theme.pad

    color: Theme.raised
    radius: Theme.radiusL
    border.width: 1
    border.color: Theme.stroke

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: root.padding
    }
}
