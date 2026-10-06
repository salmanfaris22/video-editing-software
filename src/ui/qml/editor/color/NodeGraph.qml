import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
// Serial nodes (DaVinci-style): compact tiles with left/right ports, horizontal chain.
Rectangle {
    id: root
    objectName: "nodeGraph"

    property ProjectController project
    property string clipId
    property var sel: ({})
    property string nodeId: ""
    property bool highlight: false
    signal selectNode(string id)
    signal highlightToggled()
    signal openLooks()
    signal openEffects()

    readonly property var nodes: root.sel && root.sel.nodes ? root.sel.nodes : []
    readonly property bool hasLook: !!root.sel && (root.sel.look || "").length > 0
    readonly property bool hasFilm: !!root.sel && root.sel.filmOn === true

    color: Theme.surface

    function addNode(kind) {
        const id = root.project.addNode(root.clipId, kind)
        if (id.length > 0) root.selectNode(id)
    }
    function describe(n) {
        const parts = []
        const shape = n.window ? n.window.shape : ""
        if (shape === "circle") parts.push("Circle")
        else if (shape === "rectangle") parts.push("Rect")
        else if (shape === "gradient") parts.push("Grad")
        if (n.qualifier && n.qualifier.enabled) parts.push("Key")
        if (n.subject === "person") parts.push("Person")
        else if (n.subject === "background") parts.push("BG")
        let text = parts.length > 0 ? parts.join("·") : "All"
        if (n.invert) text = "Out·" + text
        return text
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Layout.leftMargin: 8
            Layout.rightMargin: 6
            spacing: 6
            Label { text: "Nodes"; color: Theme.textSecondary; font.pixelSize: Theme.fontS; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            IconButton {
                id: addButton
                objectName: "addNode"
                iconName: "plus"
                iconSize: 14
                tooltip: "Add node (⌥S)"
                enabled: root.clipId.length > 0 && root.nodes.length < 8
                onClicked: addMenu.popup(addButton, 0, addButton.height + 2)
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.stroke }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: height
            contentWidth: chain.implicitWidth + 24
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Row {
                id: chain
                anchors.verticalCenter: parent.verticalCenter
                x: 12
                height: 48
                spacing: 0

                PortLabel { label: "IN" }

                Wire { }

                ResolveNode {
                    objectName: "node-01"
                    number: "01"
                    sub: "All"
                    selected: root.nodeId.length === 0
                    onPicked: root.selectNode("")
                    onMenuRequested: primaryMenu.popup()
                }

                Repeater {
                    model: root.nodes
                    delegate: ResolveNode {
                        required property var modelData
                        required property int index
                        objectName: "node-" + modelData.id
                        number: (index + 2).toString().padStart(2, "0")
                        sub: root.describe(modelData)
                        selected: root.nodeId === modelData.id
                        enabledNode: modelData.enabled !== false
                        onPicked: root.selectNode(modelData.id)
                        onMenuRequested: {
                            nodeMenu.node = modelData
                            nodeMenu.index = index
                            nodeMenu.popup()
                        }
                    }
                }

                ResolveNode {
                    visible: root.hasLook
                    number: "LK"
                    sub: "Look"
                    accent: "#A78BFA"
                    onPicked: root.openLooks()
                }
                ResolveNode {
                    visible: root.hasFilm
                    number: "FX"
                    sub: "Film"
                    accent: "#F59E0B"
                    onPicked: root.openEffects()
                }

                Wire { }
                PortLabel { label: "OUT" }
            }
        }
    }

    component PortLabel: Label {
        property string label
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        text: label
        color: Theme.textFaint
        font.pixelSize: 9
        font.letterSpacing: 0.5
        leftPadding: 2
        rightPadding: 2
    }

    component Wire: Item {
        width: 14
        height: 48
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 2
            color: Theme.strokeStrong
        }
    }

    component ResolveNode: Item {
        id: node
        property string number: "01"
        property string sub: ""
        property bool selected: false
        property bool enabledNode: true
        property color accent: Theme.accent
        signal picked()
        signal menuRequested()
        signal doubleClicked()

        width: 52
        height: 48

        ToolTip.visible: nodeMouse.containsMouse && node.sub.length > 0
        ToolTip.text: node.number + (node.sub.length ? " · " + node.sub : "")
        ToolTip.delay: 400

        Rectangle {
            id: body
            anchors.centerIn: parent
            width: 44
            height: 28
            radius: 0
            color: node.selected ? Theme.selected : nodeMouse.containsMouse ? Theme.hover : Theme.inset
            border.width: node.selected ? 2 : 1
            border.color: node.selected ? node.accent : Theme.strokeStrong
            opacity: node.enabledNode ? 1 : 0.4

            Label {
                anchors.centerIn: parent
                text: node.number
                color: node.selected ? Theme.text : Theme.textMuted
                font.pixelSize: 11
                font.weight: Font.Bold
                font.family: Theme.monoFamily
            }

            Rectangle {
                x: -4
                anchors.verticalCenter: parent.verticalCenter
                width: 7
                height: 7
                radius: 3.5
                color: "#D4A017"
                border.width: 1
                border.color: "#0B0C0F"
            }
            Rectangle {
                x: parent.width - 3
                anchors.verticalCenter: parent.verticalCenter
                width: 7
                height: 7
                radius: 3.5
                color: "#D4A017"
                border.width: 1
                border.color: "#0B0C0F"
            }
        }

        MouseArea {
            id: nodeMouse
            anchors.fill: body
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                node.picked()
                if (mouse.button === Qt.RightButton) node.menuRequested()
            }
            onDoubleClicked: node.doubleClicked()
        }
    }

    LecternMenu {
        id: addMenu
        LecternMenuItem { text: "Serial node   ⌥S"; onTriggered: root.addNode("") }
        LecternMenuSeparator {}
        LecternMenuItem { text: "Circle window"; onTriggered: root.addNode("circle") }
        LecternMenuItem { text: "Rectangle window"; onTriggered: root.addNode("rectangle") }
        LecternMenuItem { text: "Gradient window"; onTriggered: root.addNode("gradient") }
        LecternMenuSeparator {}
        LecternMenuItem { text: "Color key"; onTriggered: root.addNode("color") }
        LecternMenuItem { text: "Person mask"; enabled: root.project.segmentationAvailable; onTriggered: root.addNode("person") }
        LecternMenuItem { text: "Background mask"; enabled: root.project.segmentationAvailable; onTriggered: root.addNode("background") }
    }
    LecternMenu {
        id: primaryMenu
        LecternMenuItem { text: "Reset correction"; onTriggered: root.project.resetColor(root.clipId) }
        LecternMenuItem { text: "Add node   ⌥S"; enabled: root.nodes.length < 8; onTriggered: root.addNode("") }
    }
    LecternMenu {
        id: nodeMenu
        property var node: ({})
        property int index: 0
        LecternMenuItem { text: nodeMenu.node.enabled === false ? "Enable node" : "Disable node"; onTriggered: root.project.setNodeEnabled(root.clipId, nodeMenu.node.id, nodeMenu.node.enabled === false) }
        LecternMenuItem { text: "Reset node grade"; onTriggered: root.project.resetNode(root.clipId, nodeMenu.node.id) }
        LecternMenuItem { text: nodeMenu.node.invert ? "Grade selection" : "Grade outside"; onTriggered: root.project.setNodeInvert(root.clipId, nodeMenu.node.id, !nodeMenu.node.invert) }
        LecternMenuItem { text: "Highlight selection   ⇧H"; onTriggered: { root.selectNode(nodeMenu.node.id); root.highlightToggled() } }
        LecternMenuSeparator {}
        LecternMenuItem { text: "Move earlier"; enabled: nodeMenu.index > 0; onTriggered: root.project.moveNode(root.clipId, nodeMenu.node.id, -1) }
        LecternMenuItem { text: "Move later"; enabled: nodeMenu.index < root.nodes.length - 1; onTriggered: root.project.moveNode(root.clipId, nodeMenu.node.id, 1) }
        LecternMenuSeparator {}
        LecternMenuItem {
            text: "Delete node"
            onTriggered: {
                if (root.nodeId === nodeMenu.node.id) root.selectNode("")
                root.project.removeNode(root.clipId, nodeMenu.node.id)
            }
        }
    }
}
