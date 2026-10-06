import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window

ApplicationWindow {
    id: window

    required property AppController app
    readonly property RecorderController recorder: app.recorder
    readonly property bool capturing: ["recording", "paused", "stopping"].indexOf(recorder.state) >= 0

    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    visible: true
    color: Theme.bg
    title: app.view === "editor" && app.project.loaded ? app.project.title + " — " + app.productName : app.productName
    font.pixelSize: Theme.fontM

    readonly property var editor: window.app.view === "editor" ? viewLoader.item : null
    readonly property ProjectController project: window.app.project
    readonly property PlaybackController playback: window.app.playback

    // ---- Menu bar (native on macOS: shown in the system menu bar) -------------
    menuBar: MenuBar {
        Menu {
            title: "File"
            MenuItem { text: "New Recording"; onTriggered: window.app.newRecording() }
            MenuItem { text: "Open Project…"; onTriggered: window.app.goHome() }
            Menu {
                id: recentMenu
                title: "Open Recent"
                Instantiator {
                    model: window.app.recentProjects
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: window.app.openProject(modelData.path)
                    }
                    onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                }
            }
            MenuSeparator {}
            MenuItem { text: "Save"; enabled: window.project.loaded; onTriggered: window.project.save() }
            MenuItem { text: "Export…"; enabled: window.project.loaded && window.project.duration > 0; onTriggered: if (window.editor) window.editor.openExport() }
            MenuSeparator {}
            MenuItem { text: "Close Project (Home)"; enabled: window.app.view !== "home"; onTriggered: window.app.goHome() }
        }
        Menu {
            title: "Edit"
            MenuItem { text: "Undo"; enabled: window.project.canUndo; onTriggered: window.project.undo() }
            MenuItem { text: "Redo"; enabled: window.project.canRedo; onTriggered: window.project.redo() }
            MenuSeparator {}
            MenuItem { text: "Delete (leave gap)"; enabled: window.project.selectedClips.length > 0; onTriggered: window.project.deleteSelected() }
            MenuItem {
                text: "Ripple Delete (close gap)"
                enabled: window.project.selection.start !== undefined
                onTriggered: window.project.removeRange(window.project.selection.start, window.project.selection.start + window.project.selection.duration)
            }
            MenuItem { text: "Select All Clips"; enabled: window.project.loaded; onTriggered: window.project.selectAllClips() }
            MenuItem { text: "Deselect"; onTriggered: window.project.clearSelection() }
        }
        Menu {
            title: "Timeline"
            enabled: window.editor !== null
            MenuItem { text: "Split at Playhead"; onTriggered: window.project.splitAt(window.playback.position) }
            MenuItem { text: "Add Marker"; onTriggered: window.project.addMarker(window.playback.position, "") }
            MenuItem {
                text: window.project.selection.enabled === false ? "Enable Clip" : "Disable Clip"
                enabled: window.project.selectedClip.length > 0
                onTriggered: window.project.setClipEnabled(window.project.selectedClip, window.project.selection.enabled === false)
            }
            MenuSeparator {}
            MenuItem { text: "Add Text Layer"; onTriggered: window.project.addText("Your text", window.playback.position, 4.0, "title") }
            MenuItem { text: "Add Empty Layer"; onTriggered: window.project.addTrack("overlay") }
            MenuItem { text: "Add Audio Layer"; onTriggered: window.project.addTrack("audio") }
        }
        Menu {
            title: "Playback"
            enabled: window.editor !== null
            MenuItem { text: window.playback.playing ? "Pause" : "Play"; onTriggered: window.playback.toggle() }
            MenuItem { text: "Go to Start"; onTriggered: window.playback.seek(0) }
            MenuItem { text: "Go to End"; onTriggered: window.playback.seek(window.project.duration) }
            MenuItem { text: "Previous Frame"; onTriggered: window.playback.step(-1) }
            MenuItem { text: "Next Frame"; onTriggered: window.playback.step(1) }
        }
        Menu {
            title: "View"
            enabled: window.editor !== null
            MenuItem { text: "Edit Page"; checkable: true; checked: !!window.editor && window.editor.page === "edit"; onTriggered: window.editor.page = "edit" }
            MenuItem { text: "Color Page"; checkable: true; checked: !!window.editor && window.editor.page === "color"; onTriggered: window.editor.page = "color" }
            MenuSeparator {}
            Menu {
                id: scopesMenu
                title: "Scopes"
                Instantiator {
                    model: [["Parade", "parade"], ["Waveform", "waveform"], ["Vectorscope", "vectorscope"], ["Histogram", "histogram"]]
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData[0]
                        checkable: true
                        checked: !!window.editor && window.editor.scopeMode === modelData[1]
                        onTriggered: { window.editor.scopeMode = modelData[1]; window.editor.page = "color" }
                    }
                    onObjectAdded: (index, object) => scopesMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => scopesMenu.removeItem(object)
                }
            }
        }
        Menu {
            title: "Assistants"
            MenuItem {
                objectName: "mcpStatus"
                text: window.app.assistants.enabled ? "MCP server: on · " + window.app.assistants.endpoint
                                                    : "MCP server: off"
                enabled: false
            }
            MenuItem {
                text: "Allow AI Assistants (Claude, Codex, ChatGPT)"
                checkable: true
                checked: window.app.assistants.enabled
                onTriggered: window.app.assistants.enabled = checked
            }
            MenuSeparator {}
            Menu {
                id: clientsMenu
                title: "Connected Clients (" + window.app.assistants.clients.length + ")"
                enabled: window.app.assistants.clients.length > 0
                Instantiator {
                    model: window.app.assistants.clients
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name + " — " + modelData.access + " · Revoke"
                        onTriggered: window.app.assistants.revoke(modelData.id)
                    }
                    onObjectAdded: (index, object) => clientsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => clientsMenu.removeItem(object)
                }
            }
            MenuItem { text: "Copy Setup for Claude / Codex"; onTriggered: window.app.assistants.copySetup() }
            MenuItem { text: "Manage Assistants…"; onTriggered: assistantsDialog.open() }
        }
        Menu {
            title: "Help"
            MenuItem { text: "Show Logs"; onTriggered: window.app.showLogs() }
            MenuItem { text: "Keyboard Shortcuts"; onTriggered: window.app.showShortcuts() }
            MenuSeparator {}
            MenuItem { text: "About Lectern"; onTriggered: aboutDialog.open() }
        }
    }

    Dialog {
        id: aboutDialog
        anchors.centerIn: parent
        title: "About Lectern"
        modal: true
        standardButtons: Dialog.Close
        Label {
            text: window.app.productName + " " + window.app.version + "\nScreen recorder and editor\n" + window.app.platformName
            color: Theme.text
        }
    }
    AssistantsDialog {
        id: assistantsDialog
        assistants: window.app.assistants
        anchors.centerIn: parent
    }

    Loader {
        id: viewLoader
        anchors.fill: parent
        sourceComponent: window.app.view === "editor" ? editorView
                       : window.app.view === "record" ? recordView
                       : homeView
    }
    Component { id: homeView; HomeScreen { app: window.app } }
    Component { id: recordView; RecordScreen { app: window.app } }
    Component { id: editorView; EditorScreen { app: window.app } }

    CountdownOverlay {
        anchors.fill: parent
        recorder: window.recorder
        visible: window.recorder.state === "countdown"
    }

    // Floating controls while recording (its own window, excluded from capture).
    RecordingHud {
        recorder: window.recorder
        shown: window.capturing
    }

    Connections {
        target: window.app
        function onActivateRequested() {
            if (window.visibility === Window.Minimized) window.showNormal()
            window.raise()
            window.requestActivate()
        }
    }

    // Get out of the way while recording; come back when the take is saved.
    Connections {
        target: window.recorder
        function onStateChanged() {
            const s = window.recorder.state
            if (s === "recording" && window.visibility !== Window.Minimized)
                window.showMinimized()
            else if (s === "saving" || s === "idle") {
                if (window.visibility === Window.Minimized) window.showNormal()
                window.raise()
                window.requestActivate()
            }
        }
    }

    // Keyboard shortcuts (⌘ is "Ctrl" in Qt key sequences on macOS).
    // When system-wide hotkeys are registered they handle these combinations
    // (also while Lectern is in the background), so the in-app shortcuts are
    // disabled to avoid handling a key press twice.
    Shortcut {
        sequence: "Ctrl+Shift+R"
        context: Qt.ApplicationShortcut
        enabled: !window.app.globalHotkeys
        onActivated: window.app.toggleRecording()
    }
    Shortcut {
        sequence: "Ctrl+Shift+P"
        context: Qt.ApplicationShortcut
        enabled: !window.app.globalHotkeys
        onActivated: window.recorder.togglePause()
    }
    Shortcut {
        sequence: "Escape"
        enabled: window.recorder.state === "countdown"
        onActivated: window.recorder.cancelRecording()
    }
}
