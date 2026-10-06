import QtQuick
import QtQuick.Controls.Basic

// A ComboBox in the Color page's dark style.
ComboBox {
    id: control

    implicitHeight: 26
    font.pixelSize: 11
    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        radius: 3
        color: control.pressed ? Theme.pressed : control.hovered ? Theme.hover : "#1A1D23"
        border.color: control.popup.visible ? Theme.accent : "#30343E"
    }
    contentItem: Label {
        leftPadding: 8
        rightPadding: control.indicator.width + 4
        text: control.displayText
        color: control.enabled ? Theme.text : Theme.textFaint
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: control.width - width - 8
        y: (control.height - height) / 2
        name: "chevron-down"
        size: 11
        color: Theme.textMuted
    }
    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: control.width
        height: 26
        highlighted: control.highlightedIndex === index
        contentItem: Label {
            text: item.modelData
            color: item.index === control.currentIndex ? Theme.text : "#C3C7D0"
            font.pixelSize: 11
            font.weight: item.index === control.currentIndex ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { color: item.highlighted ? Theme.accentSoft : "transparent" }
    }
    popup: Popup {
        y: control.height + 2
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle { radius: 4; color: "#1A1D23"; border.color: "#363B46" }
    }
}
