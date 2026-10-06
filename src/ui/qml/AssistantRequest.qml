import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Asks the user about an AI assistant that connected (Claude, Codex …) and
// waits for approval: read only, read and edit, or deny. One request at a
// time, top right of the window; it stays until answered.
Popup {
    id: root
    objectName: "assistantRequest"
    required property McpController assistants

    readonly property var request: {
        const pending = root.assistants.clients.filter(c => c.access === "pending")
        return pending.length > 0 ? pending[0] : null
    }

    visible: !!root.request
    closePolicy: Popup.NoAutoClose
    modal: false
    padding: 16
    width: 380
    x: parent ? parent.width - width - 16 : 0
    y: 56

    background: Rectangle {
        color: Theme.raised
        radius: Theme.radiusM
        border.color: Theme.accent
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            spacing: 8
            Rectangle { width: 8; height: 8; radius: 4; color: Theme.accent }
            Label {
                Layout.fillWidth: true
                text: (root.request ? root.request.name || "An assistant" : "") + " wants to use Lectern"
                color: Theme.text
                font.pixelSize: Theme.fontM
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
        Label {
            Layout.fillWidth: true
            text: "Read only shares the open project's details with the assistant's model provider. Read and edit also lets it change the project; every change can be undone."
            color: Theme.textSecondary
            font.pixelSize: Theme.fontS
            wrapMode: Text.WordWrap
        }
        RowLayout {
            spacing: 8
            PrimaryButton {
                objectName: "requestDeny"
                text: "Deny"
                variant: "danger"
                implicitHeight: 32
                onClicked: root.assistants.deny(root.request.id)
            }
            Item { Layout.fillWidth: true }
            PrimaryButton {
                objectName: "requestRead"
                text: "Read only"
                variant: "ghost"
                implicitHeight: 32
                onClicked: root.assistants.approve(root.request.id, "read")
            }
            PrimaryButton {
                objectName: "requestEdit"
                text: "Read and edit"
                implicitHeight: 32
                onClicked: root.assistants.approve(root.request.id, "edit")
            }
        }
    }
}
