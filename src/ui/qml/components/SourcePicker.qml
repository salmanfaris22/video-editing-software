import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Device / target selector. `sources` is a list of {id, name, detail, kind?}.
// When `noneText` is set, an extra "none" entry with an empty id is offered.
ComboBox {
    id: control

    property var sources: []
    property string currentId
    property string noneText
    property string iconName
    signal picked(string id)

    readonly property var entries: noneText.length > 0
        ? [{ id: "", name: noneText, detail: "" }].concat(sources)
        : sources

    model: entries
    textRole: "name"
    valueRole: "id"
    currentIndex: {
        for (let i = 0; i < entries.length; ++i)
            if (entries[i].id === currentId) return i
        return -1
    }
    onActivated: index => control.picked(entries[index].id)

    implicitHeight: 44
    hoverEnabled: true
    font.pixelSize: Theme.fontM

    function kindIcon(entry) {
        if (!entry || entry.id === "") return control.iconName
        if (entry.id === "phone") return "phone"
        if (entry.kind === "window") return "window"
        if (entry.kind === "app") return "app"
        return control.iconName
    }

    contentItem: RowLayout {
        spacing: 10
        anchors.left: parent.left
        anchors.leftMargin: 12
        Icon {
            name: control.kindIcon(control.entries[control.currentIndex])
            size: 16
            color: Theme.textMuted
        }
        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: control.currentIndex >= 0 ? control.entries[control.currentIndex].name : "Select…"
                color: Theme.text
                font.pixelSize: Theme.fontM
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: control.currentIndex >= 0 ? (control.entries[control.currentIndex].detail || "") : ""
                color: Theme.textFaint
                font.pixelSize: Theme.fontXS
                elide: Text.ElideRight
            }
        }
    }

    indicator: Icon {
        x: control.width - width - 12
        y: (control.height - height) / 2
        name: "chevron-down"
        size: 14
        color: Theme.textMuted
    }

    background: Rectangle {
        radius: Theme.radiusM
        color: control.hovered ? Theme.hover : Theme.surface
        border.width: 1
        border.color: control.popup.visible ? Theme.accent : Theme.stroke
    }

    delegate: ItemDelegate {
        id: entry
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : control.width
        height: 44
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        contentItem: RowLayout {
            spacing: 10
            Icon {
                name: control.kindIcon(entry.modelData)
                size: 16
                color: entry.modelData.id === control.currentId ? Theme.accent : Theme.textMuted
            }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: entry.modelData.name
                    color: Theme.text
                    font.pixelSize: Theme.fontM
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: entry.modelData.detail || ""
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontXS
                    elide: Text.ElideRight
                }
            }
        }
        background: Rectangle {
            radius: Theme.radiusS
            color: entry.highlighted ? Theme.hover : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 360)
        padding: 6
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.raised
            radius: Theme.radiusM
            border.width: 1
            border.color: Theme.strokeStrong
        }
    }
}
