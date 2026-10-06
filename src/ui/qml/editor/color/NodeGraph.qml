import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The clip's node graph (Resolve's node editor, serial nodes): input → 01
// (the clip's correction) → nodes that grade part of the picture → the look
// → film print → output. Click a node to grade it with the palettes; right-
// click for its menu; + adds a node (⌥S).
Rectangle {
    id: root
    objectName: "nodeGraph"

    property ProjectController project
    property string clipId
    property var sel: ({})
    property string nodeId: ""   // "" = node 01
    property bool highlight: false
    signal selectNode(string id)
    signal highlightToggled()
    signal openLooks()
    signal openEffects()

    readonly property var nodes: root.sel && root.sel.nodes ? root.sel.nodes : []
    readonly property bool hasLook: !!root.sel && (root.sel.look || "").length > 0
    readonly property bool hasFilm: !!root.sel && root.sel.filmOn === true

    color: "#121419"

    function addNode(kind) {
        const id = root.project.addNode(root.clipId, kind)
        if (id.length > 0) root.selectNode(id)
    }
    function describe(n) {
        const parts = []
        const shape = n.window ? n.window.shape : ""
        if (shape === "circle") parts.push("Circle")
        else if (shape === "rectangle") parts.push("Rectangle")
        else if (shape === "gradient") parts.push("Gradient")
        if (n.qualifier && n.qualifier.enabled) parts.push("Key")
        if (n.subject === "person") parts.push("Person")
        else if (n.subject === "background") parts.push("Background")
        let text = parts.length > 0 ? parts.join(" · ") : "Whole picture"
        if (n.invert) text = "Outside: " + text
        return text
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            Layout.leftMargin: 10
            Layout.rightMargin: 6
            spacing: 6
            Label { text: "Nodes"; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
            Label { text: "Clip"; color: Theme.textFaint; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            PaletteButton {
                id: addButton
                objectName: "addNode"
                iconName: "plus"
                text: "Node"
                compact: true
                enabled: root.clipId.length > 0 && root.nodes.length < 8
                tip: "Add a node (⌥S)"
                onClicked: addMenu.popup(addButton, 0, addButton.height + 2)
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#23262E" }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: flow.implicitHeight + 20
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Column {
                id: flow
                x: 10
                y: 10
                width: parent.width - 20
                spacing: 0

                Marker { text: "INPUT" }
                // 01: the clip's own correction
                NodeCard {
                    objectName: "node-01"
                    number: "01"
                    title: "Correction"
                    detail: "Whole picture"
                    selected: root.nodeId.length === 0
                    onPicked: root.selectNode("")
                    onMenuRequested: primaryMenu.popup()
                }
                Repeater {
                    model: root.nodes
                    delegate: NodeCard {
                        required property var modelData
                        required property int index
                        objectName: "node-" + modelData.id
                        number: (index + 2).toString().padStart(2, "0")
                        title: modelData.label || ("Node " + (index + 2))
                        detail: root.describe(modelData)
                        selected: root.nodeId === modelData.id
                        nodeEnabled: modelData.enabled !== false
                        onPicked: root.selectNode(modelData.id)
                        onMenuRequested: {
                            nodeMenu.node = modelData
                            nodeMenu.index = index
                            nodeMenu.popup()
                        }
                        onRenamed: text => root.project.setNodeLabel(root.clipId, modelData.id, text)
                    }
                }
                NodeCard {
                    visible: root.hasLook
                    number: "LK"
                    title: root.sel.lookName || "Look"
                    detail: "Look · " + Math.round((root.sel.lookAmount || 0) * 100) + " %"
                    accentColor: "#C084FC"
                    onPicked: root.openLooks()
                }
                NodeCard {
                    visible: root.hasFilm
                    number: "FX"
                    title: "Film print"
                    detail: "Film emulation · " + Math.round((root.sel.filmAmount || 0) * 100) + " %"
                    accentColor: "#F59E0B"
                    onPicked: root.openEffects()
                }
                Connector {}
                Marker { text: "OUTPUT" }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.margins: 8
            visible: root.nodes.length === 0
            wrapMode: Text.WordWrap
            text: "01 is the clip's correction. Add nodes to grade only a part: a window, one color, the person or the background."
            color: Theme.textFaint
            font.pixelSize: 10
        }
    }

    component Connector: Item {
        width: parent ? parent.width : 0
        height: 12
        Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 1.5; height: parent.height; color: "#596070" }
    }
    component Marker: Item {
        property string text
        width: parent ? parent.width : 0
        height: 18
        Row {
            anchors.centerIn: parent
            spacing: 6
            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 9; height: 9; radius: 4.5; color: "#E0B341" }
            Label { anchors.verticalCenter: parent.verticalCenter; text: parent.parent.text; color: Theme.textFaint; font.pixelSize: 9; font.letterSpacing: 1 }
        }
    }
    component NodeCard: Column {
        id: card
        property string number
        property string title
        property string detail
        property bool selected: false
        property bool nodeEnabled: true
        property color accentColor: Theme.accent
        property bool renaming: false
        signal picked()
        signal menuRequested()
        signal renamed(string text)
        width: parent ? parent.width : 0

        Item {  // the connection from the previous node
            width: card.width
            height: 12
            Rectangle { anchors.horizontalCenter: parent.horizontalCenter; width: 1.5; height: parent.height; color: "#596070" }
        }
        Rectangle {
            width: card.width
            height: 44
            radius: 4
            color: card.selected ? "#20264A" : cardMouse.containsMouse ? "#1E2128" : "#1A1D23"
            border.color: card.selected ? card.accentColor : "#363B46"
            border.width: card.selected ? 2 : 1
            opacity: card.nodeEnabled ? 1 : 0.45
            Rectangle {
                id: badge
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 18
                radius: 3
                color: card.selected ? card.accentColor : "#2A2E37"
                Label { anchors.centerIn: parent; text: card.number; color: card.selected ? "#0B0C10" : Theme.text; font.pixelSize: 10; font.weight: Font.Bold; font.family: Theme.monoFamily }
            }
            Column {
                anchors.left: badge.right
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Label {
                    visible: !card.renaming
                    width: parent.width
                    text: card.title
                    color: Theme.text
                    font.pixelSize: 11
                    font.weight: card.selected ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                Label {
                    width: parent.width
                    text: card.nodeEnabled ? card.detail : "Off · " + card.detail
                    color: Theme.textMuted
                    font.pixelSize: 9
                    elide: Text.ElideRight
                }
            }
            TextField {
                id: renameField
                visible: card.renaming
                x: badge.x + badge.width + 6
                y: 4
                width: parent.width - x - 8
                height: 20
                font.pixelSize: 11
                padding: 2
                color: Theme.text
                background: Rectangle { color: "#0B0C10"; border.color: Theme.accent; radius: 2 }
                onAccepted: { card.renamed(text); card.renaming = false }
                onActiveFocusChanged: if (!activeFocus) card.renaming = false
                Keys.onEscapePressed: card.renaming = false
            }
            MouseArea {
                id: cardMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    card.picked()
                    if (mouse.button === Qt.RightButton) card.menuRequested()
                }
                onDoubleClicked: {
                    if (card.number === "01" || card.number === "LK" || card.number === "FX") return
                    card.renaming = true
                    renameField.text = card.title
                    renameField.selectAll()
                    renameField.forceActiveFocus()
                }
            }
        }
    }

    Menu {
        id: addMenu
        MenuItem { text: "Serial Node (whole picture)   ⌥S"; onTriggered: root.addNode("") }
        MenuSeparator {}
        MenuItem { text: "Circle Window"; onTriggered: root.addNode("circle") }
        MenuItem { text: "Rectangle Window"; onTriggered: root.addNode("rectangle") }
        MenuItem { text: "Gradient Window (sky)"; onTriggered: root.addNode("gradient") }
        MenuSeparator {}
        MenuItem { text: "Color Key (pick a color)"; onTriggered: root.addNode("color") }
        MenuItem { text: "Person (Magic Mask)"; enabled: root.project.segmentationAvailable; onTriggered: root.addNode("person") }
        MenuItem { text: "Background (Magic Mask)"; enabled: root.project.segmentationAvailable; onTriggered: root.addNode("background") }
    }
    Menu {
        id: primaryMenu
        MenuItem { text: "Reset Correction"; onTriggered: root.project.resetColor(root.clipId) }
        MenuItem { text: "Add Node After   ⌥S"; enabled: root.nodes.length < 8; onTriggered: root.addNode("") }
    }
    Menu {
        id: nodeMenu
        property var node: ({})
        property int index: 0
        MenuItem { text: nodeMenu.node.enabled === false ? "Enable Node" : "Disable Node"; onTriggered: root.project.setNodeEnabled(root.clipId, nodeMenu.node.id, nodeMenu.node.enabled === false) }
        MenuItem { text: "Reset Node Grade"; onTriggered: root.project.resetNode(root.clipId, nodeMenu.node.id) }
        MenuItem { text: nodeMenu.node.invert ? "Grade the Selection" : "Grade Outside the Selection"; onTriggered: root.project.setNodeInvert(root.clipId, nodeMenu.node.id, !nodeMenu.node.invert) }
        MenuItem { text: "Highlight Selection   ⇧H"; onTriggered: { root.selectNode(nodeMenu.node.id); root.highlightToggled() } }
        MenuSeparator {}
        MenuItem { text: "Move Earlier"; enabled: nodeMenu.index > 0; onTriggered: root.project.moveNode(root.clipId, nodeMenu.node.id, -1) }
        MenuItem { text: "Move Later"; enabled: nodeMenu.index < root.nodes.length - 1; onTriggered: root.project.moveNode(root.clipId, nodeMenu.node.id, 1) }
        MenuSeparator {}
        MenuItem {
            text: "Delete Node"
            onTriggered: {
                if (root.nodeId === nodeMenu.node.id) root.selectNode("")
                root.project.removeNode(root.clipId, nodeMenu.node.id)
            }
        }
    }
}
