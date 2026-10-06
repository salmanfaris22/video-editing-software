import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: root
    required property McpController assistants
    objectName: "assistantsDialog"
    title: "AI assistants"
    modal: true
    width: 660
    height: 560
    standardButtons: Dialog.Close

    contentItem: ColumnLayout {
        spacing: 12
        Switch {
            objectName: "assistantsEnabled"
            text: "Allow assistants to connect to Lectern"
            checked: root.assistants.enabled
            onToggled: root.assistants.enabled = checked
        }
        Label {
            Layout.fillWidth: true
            text: "Approve each connection below. Read access shares project details with the assistant's model provider. Edit access also allows undoable changes. Access turns off when Lectern closes."
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Label { text: root.assistants.status; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Button {
                text: "Copy connection setup"
                onClicked: root.assistants.copySetup()
            }
        }
        Label {
            visible: root.assistants.enabled && root.assistants.clients.length === 0
            text: "Connect your assistant to see its approval request here."
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            clip: true
            model: root.assistants.clients
            spacing: 6
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                Label { text: modelData.name + " · " + modelData.access; Layout.fillWidth: true; elide: Text.ElideRight }
                Button { text: "Read"; onClicked: root.assistants.approve(modelData.id, "read") }
                Button { text: "Edit"; onClicked: root.assistants.approve(modelData.id, "edit") }
                Button { text: "Undo edits"; onClicked: root.assistants.undoEdits(modelData.id) }
                Button { text: "Revoke"; onClicked: root.assistants.revoke(modelData.id) }
                Button { text: "Deny"; onClicked: root.assistants.deny(modelData.id) }
            }
        }
        Label {
            text: "Undo edits stops at your latest manual edit or another assistant's edit."
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label { text: "Recent activity"; font.bold: true }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.assistants.activity
            delegate: Label {
                required property var modelData
                width: ListView.view.width
                text: modelData.time + "  " + modelData.client + " · " + modelData.action + " · " + modelData.outcome
                elide: Text.ElideRight
                height: 24
            }
        }
    }
}
