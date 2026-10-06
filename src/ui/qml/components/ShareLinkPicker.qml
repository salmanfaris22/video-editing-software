import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Pairing link for the phone camera page (same look as SourcePicker).
Item {
    id: root

    property string url: ""
    property string statusText: ""
    property bool enabled: true
    signal copyRequested()

    readonly property string hostLine: {
        if (!url.length) return "Starting server…"
        const m = url.match(/^https?:\/\/([^/?#]+)/)
        return m ? m[1] : url
    }

    implicitHeight: 44
    implicitWidth: 240

    property bool hovered: false

    Rectangle {
        id: shell
        anchors.fill: parent
        opacity: root.enabled ? 1 : 0.55
        radius: Theme.radiusM
        color: root.hovered ? Theme.hover : Theme.surface
        border.width: 1
        border.color: popup.opened ? Theme.accent : Theme.stroke

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10
            Icon { name: "phone"; size: 16; color: Theme.textMuted }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: "Share link"
                    color: Theme.text
                    font.pixelSize: Theme.fontM
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: root.hostLine
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontXS
                    elide: Text.ElideMiddle
                }
            }
            Icon {
                name: "chevron-down"
                size: 14
                color: Theme.textMuted
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.enabled && root.url.length > 0
            onEntered: root.hovered = true
            onExited: root.hovered = false
            onClicked: popup.open()
        }
    }

    Popup {
        id: popup
        y: root.height + 4
        width: Math.max(root.width, 320)
        implicitHeight: sheet.implicitHeight + 12
        padding: 6
        contentItem: ColumnLayout {
            id: sheet
            width: parent.width
            spacing: 10

            InputField {
                Layout.fillWidth: true
                readOnly: true
                text: root.url
                selectByMouse: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StatusChip {
                    text: root.statusText.length ? root.statusText : "Waiting"
                    tone: root.statusText === "Streaming" ? "ok"
                        : root.statusText.indexOf("Waiting") === 0 ? "info"
                        : root.statusText === "Error" ? "warn" : "off"
                }
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    variant: "ghost"
                    iconName: "copy"
                    text: "Copy link"
                    onClicked: {
                        root.copyRequested()
                        popup.close()
                    }
                }
            }
        }
        background: Rectangle {
            color: Theme.raised
            radius: Theme.radiusM
            border.width: 1
            border.color: Theme.strokeStrong
        }
    }
}
