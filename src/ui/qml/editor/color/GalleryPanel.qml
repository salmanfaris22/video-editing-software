import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// The Color page's left panel (Resolve's Gallery / LUTs, CineGrade's Looks):
// Gallery — grab stills and copy their grade or wipe against them;
// Looks — cinematic looks on top of the clip's correction, with Amount;
// LUTs — camera log conversions and imported .cube files.
Rectangle {
    id: root
    objectName: "galleryPanel"

    property ProjectController project
    property PlaybackController playback
    property string clipId
    property var sel: ({})
    property string tab: "looks"
    signal compareWithStill(string stillId)

    color: Theme.surface

    readonly property bool gradable: root.clipId.length > 0 && root.sel.visual === true && root.sel.role !== "text"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {  // tabs
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            Layout.leftMargin: 6
            spacing: 2
            Repeater {
                model: [{ id: "gallery", label: "Gallery" }, { id: "looks", label: "Looks" }, { id: "luts", label: "LUTs" }]
                delegate: AbstractButton {
                    id: tabButton
                    required property var modelData
                    objectName: "galleryTab-" + modelData.id
                    readonly property bool current: root.tab === modelData.id
                    Layout.preferredHeight: 30
                    Layout.preferredWidth: tabLabel.implicitWidth + 22
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: root.tab = modelData.id
                    background: Rectangle {
                        color: "transparent"
                        Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 2; color: Theme.accent; visible: tabButton.current }
                    }
                    contentItem: Label {
                        id: tabLabel
                        text: tabButton.modelData.label
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: tabButton.current ? Theme.text : tabButton.hovered ? Theme.textSecondary : Theme.textMuted
                        font.pixelSize: 12
                        font.weight: tabButton.current ? Font.DemiBold : Font.Normal
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.stroke }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.tab === "gallery" ? 0 : root.tab === "looks" ? 1 : 2

            // ---- Gallery ------------------------------------------------------
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    PaletteButton {
                        objectName: "grabStill"
                        text: "Grab Still"
                        iconName: "image"
                        enabled: root.gradable
                        tip: "Save this frame with its grade (⌥⌘G)"
                        onClicked: root.project.grabStill(root.clipId, root.playback.position)
                    }
                    Item { Layout.fillWidth: true }
                    Label { text: root.project.stills.length + " stills"; color: Theme.textFaint; font.pixelSize: 10 }
                }
                Rectangle {
                    visible: root.playback.referenceStill.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    implicitHeight: 26
                    radius: 3
                    color: Theme.accentSoft
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        Label { Layout.fillWidth: true; text: "Wiping against a still"; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                        PaletteButton { compact: true; text: "Stop"; onClicked: root.playback.setReferenceStill("") }
                    }
                }
                GridView {
                    id: stillsGrid
                    objectName: "stills"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 8
                    clip: true
                    cellWidth: Math.max(100, (width - 2) / 2)
                    cellHeight: cellWidth * 9 / 16 + 22
                    model: root.project.stills
                    delegate: Item {
                        required property var modelData
                        required property int index
                        objectName: "still-" + index
                        width: stillsGrid.cellWidth
                        height: stillsGrid.cellHeight
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 3
                            radius: 3
                            color: stillMouse.containsMouse ? Theme.hover : Theme.raised
                            border.color: stillMouse.containsMouse ? Theme.strokeStrong : Theme.stroke
                            Image {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 3
                                height: width * 9 / 16
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                source: parent.parent.modelData.image
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 4
                                text: parent.parent.modelData.label
                                color: Theme.textMuted
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: stillMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onDoubleClicked: if (root.gradable) root.project.applyStill(parent.parent.modelData.id, root.clipId)
                                onClicked: mouse => {
                                    if (mouse.button !== Qt.RightButton) return
                                    stillMenu.still = parent.parent.modelData
                                    stillMenu.popup()
                                }
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 600
                                ToolTip.text: parent.parent.modelData.clipName + " · double-click to apply its grade"
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: root.project.stills.length === 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 40
                    Label { Layout.alignment: Qt.AlignHCenter; text: "No stills created"; color: Theme.textFaint; font.pixelSize: 13 }
                    Label {
                        Layout.maximumWidth: 220
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: "Grab a still to keep a grade: apply it to other clips, or wipe against it."
                        color: Theme.textFaint
                        font.pixelSize: 10
                    }
                }
            }

            // ---- Looks --------------------------------------------------------
            ColumnLayout {
                id: looksTab
                spacing: 6
                property string category: "All"
                property string hovered: ""
                readonly property var allLooks: root.project.looks
                readonly property var categories: {
                    const out = ["All"]
                    for (const l of looksTab.allLooks) if (out.indexOf(l.category) < 0) out.push(l.category)
                    return out
                }
                readonly property var shown: {
                    const out = [{ id: "none", name: "None", category: "", description: "The clip without a look", custom: false }]
                    for (const l of looksTab.allLooks) if (looksTab.category === "All" || l.category === looksTab.category) out.push(l)
                    return out
                }

                function refresh() { if (root.gradable && root.tab === "looks") root.project.refreshLookPreviews(root.clipId, root.playback.position) }
                Timer { id: refreshLater; interval: 350; onTriggered: looksTab.refresh() }
                Connections {
                    target: root
                    function onClipIdChanged() { refreshLater.restart() }
                    function onTabChanged() { refreshLater.restart() }
                }
                Connections {
                    target: root.project
                    function onProjectChanged() { if (root.tab === "looks") refreshLater.restart() }
                }
                Connections {
                    target: root.playback
                    function onPositionChanged() { if (!root.playback.playing && root.tab === "looks") refreshLater.restart() }
                }
                Component.onCompleted: refreshLater.restart()

                Flow {  // categories
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 8
                    spacing: 4
                    Repeater {
                        model: looksTab.categories
                        delegate: PaletteButton {
                            required property var modelData
                            compact: true
                            text: modelData
                            checkable: true
                            checked: looksTab.category === modelData
                            onClicked: looksTab.category = modelData
                        }
                    }
                }
                GridView {
                    id: looksGrid
                    objectName: "looksGrid"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 6
                    clip: true
                    cellWidth: Math.max(96, (width - 2) / 2)
                    cellHeight: cellWidth * 9 / 16 + 24
                    model: looksTab.shown
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Item {
                        id: tile
                        required property var modelData
                        objectName: "look-" + modelData.id
                        readonly property bool current: modelData.id === "none" ? !(root.sel.look) : root.sel.look === modelData.id
                        width: looksGrid.cellWidth
                        height: looksGrid.cellHeight
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 3
                            radius: 3
                            color: tileMouse.containsMouse ? Theme.hover : Theme.raised
                            border.color: tile.current ? Theme.accent : tileMouse.containsMouse ? Theme.strokeStrong : Theme.stroke
                            border.width: tile.current ? 2 : 1
                            Image {
                                id: thumb
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 3
                                height: width * 9 / 16
                                fillMode: Image.PreserveAspectCrop
                                cache: false
                                source: "image://look/" + tile.modelData.id + "?r=" + root.project.lookPreviewRevision
                            }
                            Label {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: 4
                                text: tile.modelData.name
                                color: tile.current ? Theme.text : Theme.textSecondary
                                font.pixelSize: 10
                                font.weight: tile.current ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: tileMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: root.gradable
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                Timer {
                                    id: hoverPreview
                                    interval: 160
                                    onTriggered: {
                                        looksTab.hovered = tile.modelData.id
                                        root.project.previewLook(root.clipId, tile.modelData.id)
                                    }
                                }
                                onEntered: hoverPreview.restart()
                                onExited: {
                                    hoverPreview.stop()
                                    if (looksTab.hovered === tile.modelData.id) {
                                        looksTab.hovered = ""
                                        root.project.cancelPreview()
                                    }
                                }
                                onClicked: mouse => {
                                    if (mouse.button === Qt.RightButton) {
                                        if (tile.modelData.custom) { lookMenu.look = tile.modelData; lookMenu.popup() }
                                        return
                                    }
                                    hoverPreview.stop()
                                    looksTab.hovered = ""
                                    if (tile.modelData.id === "none") root.project.removeLook(root.clipId)
                                    else root.project.applyLook(root.clipId, tile.modelData.id, root.sel.look ? root.sel.lookAmount : 1.0)
                                }
                                ToolTip.visible: containsMouse
                                ToolTip.delay: 700
                                ToolTip.text: tile.modelData.description
                            }
                        }
                    }
                }
                ColumnLayout {  // the clip's look: amount, apply to all, save
                    Layout.fillWidth: true
                    Layout.margins: 8
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: root.sel.look ? (root.sel.lookName || "Look") : "No look"
                            color: root.sel.look ? Theme.text : Theme.textFaint
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        Label { text: Math.round((root.sel.lookAmount || 0) * 100) + " %"; color: Theme.textMuted; font.pixelSize: 11; font.family: Theme.monoFamily; visible: !!root.sel.look }
                    }
                    MiniSlider {
                        objectName: "lookAmount"
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        enabled: !!root.sel.look
                        value: root.sel.lookAmount === undefined ? 1 : root.sel.lookAmount
                        onMoved: root.project.setLookAmount(root.clipId, value)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        PaletteButton { objectName: "lookToAll"; compact: true; text: "Apply to all"; enabled: !!root.sel.look; tip: "This look and amount on every clip"; onClicked: root.project.applyLookToAll(root.clipId) }
                        PaletteButton { compact: true; text: "Remove"; enabled: !!root.sel.look; onClicked: root.project.removeLook(root.clipId) }
                        Item { Layout.fillWidth: true }
                        PaletteButton { objectName: "saveLook"; compact: true; text: "Save…"; enabled: root.gradable; tip: "Save this clip's whole grade to My Looks"; onClicked: { saveRow.visible = true; lookName.forceActiveFocus() } }
                    }
                    RowLayout {
                        id: saveRow
                        visible: false
                        Layout.fillWidth: true
                        TextField {
                            id: lookName
                            objectName: "lookName"
                            Layout.fillWidth: true
                            placeholderText: "Look name"
                            font.pixelSize: 11
                            color: Theme.text
                            background: Rectangle { color: Theme.inset; border.color: lookName.activeFocus ? Theme.accent : Theme.strokeStrong; radius: Theme.radiusS }
                            onAccepted: saveButton.clicked()
                            Keys.onEscapePressed: saveRow.visible = false
                        }
                        PaletteButton {
                            id: saveButton
                            compact: true
                            text: "Save"
                            enabled: lookName.text.trim().length > 0
                            onClicked: {
                                if (root.project.saveLook(root.clipId, lookName.text).length > 0) {
                                    lookName.text = ""
                                    saveRow.visible = false
                                    looksTab.category = "My Looks"
                                }
                            }
                        }
                    }
                }
            }

            // ---- LUTs ---------------------------------------------------------
            ColumnLayout {
                spacing: 6
                ListView {
                    id: lutList
                    objectName: "lutList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: 6
                    clip: true
                    model: {
                        const out = [{ id: "", name: "None" }]
                        for (const l of root.project.builtinLuts) out.push(l)
                        const cur = root.sel.lut || ""
                        if (cur.length > 0 && cur.indexOf("builtin:") !== 0) out.push({ id: cur, name: "Imported: " + (root.sel.lutName || cur) })
                        return out
                    }
                    delegate: ItemDelegate {
                        required property var modelData
                        width: lutList.width
                        height: 30
                        readonly property bool current: (root.sel.lut || "") === modelData.id
                        enabled: root.gradable
                        onClicked: root.project.setColorLut(root.clipId, modelData.id)
                        background: Rectangle { color: parent.current ? Theme.accentSoft : parent.hovered ? Theme.hover : "transparent" }
                        contentItem: Label {
                            leftPadding: 6
                            text: parent.modelData.name
                            color: parent.current ? Theme.text : Theme.textSecondary
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    spacing: 6
                    RowLayout {
                        Label { text: "LUT mix"; color: Theme.textMuted; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Label { text: Math.round((root.sel.lutAmount === undefined ? 1 : root.sel.lutAmount) * 100) + " %"; color: Theme.textMuted; font.pixelSize: 11; font.family: Theme.monoFamily }
                    }
                    MiniSlider {
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        enabled: (root.sel.lut || "").length > 0
                        value: root.sel.lutAmount === undefined ? 1 : root.sel.lutAmount
                        onMoved: root.project.setColorValue(root.clipId, "lutAmount", value)
                    }
                    PaletteButton { text: "Import .cube…"; iconName: "upload"; enabled: root.gradable; onClicked: cubeDialog.open() }
                }
            }
        }
    }

    FileDialog {
        id: cubeDialog
        title: "Import a 3D LUT"
        nameFilters: ["Cube LUTs (*.cube)"]
        onAccepted: root.project.importLut(root.clipId, selectedFile)
    }
    Menu {
        id: stillMenu
        property var still: ({})
        MenuItem { text: "Apply Grade"; enabled: root.gradable; onTriggered: root.project.applyStill(stillMenu.still.id, root.clipId) }
        MenuItem { text: "Wipe Against This Still"; onTriggered: root.compareWithStill(stillMenu.still.id) }
        MenuSeparator {}
        MenuItem { text: "Delete Still"; onTriggered: root.project.deleteStill(stillMenu.still.id) }
    }
    Menu {
        id: lookMenu
        property var look: ({})
        MenuItem { text: "Apply"; enabled: root.gradable; onTriggered: root.project.applyLook(root.clipId, lookMenu.look.id, 1.0) }
        MenuItem { text: "Delete Look"; onTriggered: root.project.deleteLook(lookMenu.look.id) }
    }
}
