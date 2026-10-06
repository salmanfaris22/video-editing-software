import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property AppController app
    signal exportRequested()

    implicitHeight: 52
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.stroke
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 14
        spacing: 8

        IconButton {
            iconName: "home"
            tooltip: "Home"
            onClicked: root.app.goHome()
        }
        Rectangle { width: 1; height: 22; color: Theme.stroke }
        ColumnLayout {
            spacing: 0
            Label {
                text: root.app.project.title
                color: Theme.text
                font.pixelSize: Theme.fontL
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: 420
            }
            Label {
                text: root.app.project.canvasWidth + " × " + root.app.project.canvasHeight + " · "
                      + root.app.project.frameRateText + " fps · " + root.app.project.formatTime(root.app.project.duration)
                color: Theme.textFaint
                font.pixelSize: Theme.fontXS
            }
        }
        StatusChip {
            visible: root.app.project.notice.length > 0
            text: "Restored from backup"
            tone: "warn"
        }
        StatusChip {
            visible: root.app.project.busy
            text: root.app.project.busyText
            tone: "info"
        }
        Item { Layout.fillWidth: true }

        IconButton {
            iconName: "undo"
            tooltip: "Undo (⌘Z)"
            enabled: root.app.project.canUndo
            onClicked: root.app.project.undo()
        }
        IconButton {
            iconName: "redo"
            tooltip: "Redo (⌘⇧Z)"
            enabled: root.app.project.canRedo
            onClicked: root.app.project.redo()
        }
        Item { Layout.fillWidth: true }

        ThemeToggle {
            objectName: "themeToggle"
            app: root.app
        }
        IconButton {
            iconName: "folder"
            tooltip: "Show project in Finder"
            onClicked: root.app.revealInFinder(root.app.project.path)
        }
        Label {
            text: root.app.exporter.running ? root.app.exporter.status : root.app.project.dirty ? "Saving…" : ""
            color: Theme.textFaint
            font.pixelSize: Theme.fontXS
        }
        PrimaryButton {
            text: "Export"
            iconName: "export"
            hint: "⌘E"
            implicitHeight: 34
            enabled: root.app.project.duration > 0
            onClicked: root.exportRequested()
        }
    }
}
