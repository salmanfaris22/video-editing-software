import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: control

    // accent | record | ghost | danger
    property string variant: "accent"
    property string iconName
    property string hint   // keyboard shortcut shown at the right

    readonly property color fillColor: {
        if (!enabled) return Theme.raised
        switch (variant) {
        case "record": return down ? "#E8394A" : hovered ? Theme.recordHover : Theme.record
        case "danger": return down ? Theme.recordSoft : hovered ? "#4A1F26" : "#2E1519"
        case "ghost": return down ? Theme.pressed : hovered ? Theme.hover : "transparent"
        default: return down ? "#6A7AF0" : hovered ? Theme.accentHover : Theme.accent
        }
    }
    readonly property color labelColor: !enabled ? Theme.textFaint
        : variant === "ghost" ? Theme.text
        : variant === "danger" ? Theme.danger
        : "#FFFFFF"

    implicitHeight: 40
    focusPolicy: Qt.NoFocus
    leftPadding: 16
    rightPadding: 16
    hoverEnabled: true
    font.pixelSize: Theme.fontM
    font.weight: Font.DemiBold

    contentItem: RowLayout {
        spacing: 8
        Icon {
            visible: control.iconName.length > 0
            name: control.iconName
            size: 16
            color: control.labelColor
        }
        Label {
            Layout.fillWidth: true
            text: control.text
            font: control.font
            color: control.labelColor
            horizontalAlignment: control.hint.length > 0 ? Text.AlignLeft : Text.AlignHCenter
            elide: Text.ElideRight
        }
        Label {
            visible: control.hint.length > 0
            text: control.hint
            font.pixelSize: Theme.fontXS
            color: control.labelColor
            opacity: 0.7
        }
    }

    background: Rectangle {
        radius: Theme.radiusM
        color: control.fillColor
        border.width: control.variant === "ghost" ? 1 : 0
        border.color: Theme.strokeStrong
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
