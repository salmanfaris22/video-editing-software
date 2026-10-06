import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Project library: import files, browse media, timeline clips, folders, saved edit variants.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback
    property AppController app

    readonly property string storageKey: "libraryTree/" + (root.project.path.length ? root.project.path : "none")

    property var tree: ({ folders: [], assignments: {} })
    property string selectedFolderId: "all"
    property int section: 0  // 0 clips · 1 media · 2 edits

    function loadTree() {
        const raw = root.app ? root.app.workspaceValue(root.storageKey, "{}") : "{}"
        try {
            const parsed = typeof raw === "string" ? JSON.parse(raw) : raw
            root.tree = parsed && parsed.folders ? parsed : { folders: [], assignments: {} }
        } catch (e) {
            root.tree = { folders: [], assignments: {} }
        }
        if (root.tree.folders.length === 0) {
            root.tree.folders = [
                { id: "imports", name: "Imports" },
                { id: "recording", name: "Recording" },
                { id: "podcast", name: "Podcast" }
            ]
            root.saveTree()
        }
    }
    function saveTree() {
        if (!root.app) return
        root.app.setWorkspaceValue(root.storageKey, JSON.stringify(root.tree))
    }
    function assignItem(itemKey, folderId) {
        if (!folderId || folderId === "all") {
            delete root.tree.assignments[itemKey]
        } else {
            root.tree.assignments[itemKey] = folderId
        }
        root.saveTree()
        root.tree = JSON.parse(JSON.stringify(root.tree))
    }
    function folderName(id) {
        if (id === "all") return "All"
        for (let i = 0; i < root.tree.folders.length; ++i)
            if (root.tree.folders[i].id === id) return root.tree.folders[i].name
        return id
    }
    function inFolder(itemKey) {
        if (root.selectedFolderId === "all") return true
        return root.tree.assignments[itemKey] === root.selectedFolderId
    }

    Component.onCompleted: root.loadTree()
    onStorageKeyChanged: root.loadTree()

    FileDialog {
        id: importDialog
        title: "Import into project"
        nameFilters: [
            "All supported (*.mp4 *.mov *.mkv *.webm *.png *.jpg *.jpeg *.webp *.mp3 *.m4a *.wav *.aac *.flac)",
            "Video (*.mp4 *.mov *.mkv *.webm)",
            "Audio (*.mp3 *.m4a *.wav *.aac *.flac)",
            "Images (*.png *.jpg *.jpeg *.webp)"
        ]
        onAccepted: {
            const path = selectedFile.toString().toLowerCase()
            const audio = /\.(mp3|m4a|aac|wav|aif|aiff|flac|ogg)$/.test(path)
            root.project.importMedia(selectedFile, audio ? "music" : "overlay", root.playback.position)
        }
    }

    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Icon { name: "folder"; size: 18; color: Theme.accent }
            Label {
                Layout.fillWidth: true
                text: "Library"
                color: Theme.text
                font.pixelSize: Theme.fontL
                font.weight: Font.DemiBold
            }
            IconButton {
                iconName: "upload"
                tooltip: "Import file at playhead"
                enabled: root.project.loaded && !root.project.busy
                onClicked: importDialog.open()
            }
            IconButton {
                iconName: "folder"
                tooltip: "New folder"
                onClicked: {
                    const id = "f" + Date.now()
                    root.tree.folders.push({ id: id, name: "Folder " + (root.tree.folders.length + 1) })
                    root.saveTree()
                    root.tree = JSON.parse(JSON.stringify(root.tree))
                }
            }
        }

        Segmented {
            Layout.fillWidth: true
            iconMode: true
            options: [
                { label: "Timeline clips", value: 0, icon: "cut" },
                { label: "Media files", value: 1, icon: "folder" },
                { label: "Saved edits", value: 2, icon: "copy" }
            ]
            value: root.section
            onSelected: v => root.section = v
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [{ id: "all", name: "All" }].concat(root.tree.folders)
                delegate: AbstractButton {
                    required property var modelData
                    implicitHeight: 26
                    implicitWidth: folderChip.implicitWidth + 12
                    hoverEnabled: true
                    onClicked: root.selectedFolderId = modelData.id
                    background: Rectangle {
                        radius: Theme.radiusS
                        color: root.selectedFolderId === modelData.id ? Theme.accentSoft
                             : parent.hovered ? Theme.hover : Theme.raised
                        border.width: root.selectedFolderId === modelData.id ? 1 : 0
                        border.color: Theme.accent
                    }
                    contentItem: Label {
                        id: folderChip
                        anchors.centerIn: parent
                        text: modelData.name
                        font.pixelSize: Theme.fontXS
                        color: root.selectedFolderId === modelData.id ? Theme.text : Theme.textMuted
                    }
                }
            }
        }

        // ---- Timeline clips ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.section === 0
            spacing: 4
            Repeater {
                model: root.project.timelineClipsFlat()
                delegate: AbstractButton {
                    required property var modelData
                    readonly property string itemKey: "clip:" + modelData.id
                    visible: root.inFolder(itemKey)
                    Layout.fillWidth: true
                    implicitHeight: 40
                    hoverEnabled: true
                    onClicked: {
                        root.playback.seek(modelData.start + 0.01)
                        root.project.selectClip(modelData.id)
                    }
                    background: Rectangle {
                        radius: Theme.radiusS
                        color: parent.hovered ? Theme.hover : Theme.raised
                    }
                    leftPadding: 8  // padding belongs to the button: layouts have none
                    contentItem: RowLayout {
                        spacing: 8
                        Rectangle {
                            width: 4; height: 28; radius: 2
                            color: modelData.color || Theme.accent
                        }
                        Icon { name: modelData.role === "overlay" ? "image" : modelData.role === "camera" ? "camera" : "screen"; size: 16; color: Theme.textMuted }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                elide: Text.ElideRight
                                color: Theme.text
                                font.pixelSize: Theme.fontS
                            }
                            Label {
                                text: modelData.trackLabel + " · " + root.project.formatTime(modelData.start)
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontXS
                            }
                        }
                        IconButton {
                            iconName: "folder"
                            iconSize: 14
                            tooltip: "Move to folder"
                            onClicked: {
                                folderMenu.itemKey = itemKey
                                folderMenu.popup()
                            }
                        }
                    }
                }
            }
        }

        // ---- Media assets ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.section === 1
            spacing: 4
            Repeater {
                model: root.project.mediaAssets
                delegate: AbstractButton {
                    required property var modelData
                    readonly property string itemKey: "media:" + modelData.id
                    visible: root.inFolder(itemKey)
                    Layout.fillWidth: true
                    implicitHeight: 40
                    hoverEnabled: true
                    onClicked: root.project.placeMediaAt(modelData.id, modelData.kind === "audio" ? "music" : "overlay", root.playback.position)
                    background: Rectangle {
                        radius: Theme.radiusS
                        color: parent.hovered ? Theme.hover : Theme.raised
                    }
                    leftPadding: 8  // padding belongs to the button: layouts have none
                    contentItem: RowLayout {
                        spacing: 8
                        Icon { name: modelData.icon; size: 16; color: Theme.textMuted }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                elide: Text.ElideRight
                                color: Theme.text
                                font.pixelSize: Theme.fontS
                            }
                            Label {
                                text: modelData.kind
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontXS
                            }
                        }
                        IconButton {
                            iconName: "plus"
                            iconSize: 14
                            tooltip: "Add at playhead"
                            onClicked: root.project.placeMediaAt(modelData.id, modelData.kind === "audio" ? "music" : "overlay", root.playback.position)
                        }
                    }
                }
            }
        }

        // ---- Saved edit variants ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.section === 2
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                InputField {
                    id: editNameField
                    Layout.fillWidth: true
                    placeholderText: "Edit name (e.g. Host A rough)"
                }
                IconButton {
                    iconName: "copy"
                    tooltip: "Save a copy of this project as a separate edit"
                    enabled: root.project.loaded && editNameField.text.trim().length > 0
                    onClicked: {
                        root.project.saveEditVariant(editNameField.text.trim())
                        editNameField.text = ""
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Each saved edit is a full project copy under edits/ — open one to work on a separate podcast cut, then switch back to the main project."
                color: Theme.textFaint
                font.pixelSize: Theme.fontXS
            }
            Repeater {
                model: root.project.editVariants
                delegate: AbstractButton {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 44
                    hoverEnabled: true
                    onClicked: root.project.openEditVariant(modelData.path)
                    background: Rectangle {
                        radius: Theme.radiusS
                        color: parent.hovered ? Theme.hover : Theme.raised
                    }
                    leftPadding: 8  // padding belongs to the button: layouts have none
                    contentItem: RowLayout {
                        spacing: 8
                        Icon { name: "copy"; size: 16; color: Theme.accent }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                text: modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontS
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: modelData.modified
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontXS
                            }
                        }
                        Icon { name: "chevron-right"; size: 14; color: Theme.textFaint }
                    }
                }
            }
        }
    }

    LecternMenu {
        id: folderMenu
        property string itemKey: ""
        Repeater {
            model: root.tree.folders
            delegate: LecternMenuItem {
                required property var modelData
                text: "→ " + modelData.name
                onTriggered: root.assignItem(folderMenu.itemKey, modelData.id)
            }
        }
        LecternMenuItem {
            text: "Clear folder"
            onTriggered: root.assignItem(folderMenu.itemKey, "")
        }
    }
}
