import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Magic Mask (Resolve): select the person or the background with the AI
// person segmentation and grade them separately — brighten the face, cool
// the room. On node 01, a choice adds a node with that selection.
Item {
    id: root
    objectName: "maskPalette"

    property ProjectController project
    property string clipId
    property var node: null
    signal nodeCreated(string nodeId)

    readonly property string subject: root.node ? (root.node.subject || "") : ""
    readonly property bool available: root.project.segmentationAvailable

    function choose(subject) {
        if (!root.node) {
            if (subject.length === 0) return
            const id = root.project.addNode(root.clipId, subject)
            if (id.length > 0) root.nodeCreated(id)
            return
        }
        root.project.setNodeSubject(root.clipId, root.node.id, subject)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        RowLayout {
            spacing: 6
            Label { text: "Magic Mask"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
            Item { width: 8 }
            Repeater {
                model: [
                    { id: "person", label: "Person", tip: "Grade only the person (AI segmentation)" },
                    { id: "background", label: "Background", tip: "Grade everything behind the person" },
                    { id: "", label: "Whole picture", tip: "No person limit" }
                ]
                delegate: PaletteButton {
                    required property var modelData
                    objectName: "mask-" + (modelData.id.length > 0 ? modelData.id : "none")
                    text: modelData.label
                    tip: modelData.tip
                    checkable: true
                    checked: !!root.node && root.subject === modelData.id
                    enabled: root.available && (!!root.node || modelData.id.length > 0)
                    onClicked: root.choose(modelData.id)
                }
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: root.available ? Theme.textMuted : Theme.warning
            font.pixelSize: 11
            text: !root.available ? "Person segmentation is not available on this computer."
                  : !root.node ? "Choose Person or Background: a node is added after the clip's correction, and its grade changes only that part. Combine it with a window or a color key in the same node."
                  : root.subject === "person" ? "This node changes only the person. Use the wheels and curves to grade them — e.g. warm the skin or lift the face."
                  : root.subject === "background" ? "This node changes only the background — e.g. darken or desaturate the room so the person stands out."
                  : "This node is not limited to the person or the background."
        }
        Item { Layout.fillHeight: true }
    }
}
