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

    header: ToolBar {
        visible: !window.capturing
        height: 32
        background: Rectangle { color: Theme.surface }
        ToolButton {
            objectName: "assistantsButton"
            anchors.right: parent.right
            height: parent.height
            text: window.app.assistants.enabled ? "AI assistants · On" : "AI assistants"
            onClicked: assistantsDialog.open()
        }
    }
    AssistantsDialog {
        id: assistantsDialog
        assistants: window.app.assistants
        anchors.centerIn: parent
    }

    Loader {
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
