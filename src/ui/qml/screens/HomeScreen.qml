import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root

    required property AppController app

    FolderDialog {
        id: openDialog
        title: "Open project"
        onAccepted: root.app.openProjectUrl(selectedFolder)
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.max(48, parent.height * 0.1)
        width: Math.min(parent.width - 96, 1040)
        spacing: 28

        // Brand
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Image {  // app icon (assets/brand/lectern-app-icon.svg, pre-rendered)
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                source: "qrc:/qt/qml/Lectern/UI/brand/lectern-icon-256.png"
                sourceSize: Qt.size(112, 112)
                smooth: true
                mipmap: true
            }
            ColumnLayout {
                spacing: 2
                Label {
                    text: root.app.productName
                    color: Theme.text
                    font.pixelSize: 26
                    font.weight: Font.Bold
                }
                Label {
                    text: "Record lessons once. Edit every track."
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontL
                }
            }
            Item { Layout.fillWidth: true }
            ThemeToggle { app: root.app }
            StatusChip {
                visible: root.app.syntheticSources
                text: "Synthetic sources"
                tone: "info"
            }
        }

        Banner {
            Layout.fillWidth: true
            text: root.app.banner
            tone: root.app.bannerProject.length > 0 ? "info" : "warn"
            actionText: root.app.bannerProject.length > 0 ? "Open" : ""
            onAction: root.app.openProject(root.app.bannerProject)
            onDismissed: root.app.dismissBanner()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            // Primary action
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 236
                radius: 18
                border.width: 1
                border.color: Theme.stroke
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.dark ? "#1A1C2A" : "#FFFFFF" }
                    GradientStop { position: 1.0; color: Theme.dark ? "#13151B" : "#EEF1F8" }
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 28
                    spacing: 10
                    Rectangle {
                        width: 50
                        height: 50
                        radius: 15
                        color: Theme.recordSoft
                        Rectangle {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            radius: 9
                            color: Theme.record
                        }
                    }
                    Label {
                        text: "New recording"
                        color: Theme.text
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                    Item { Layout.fillHeight: true }
                    PrimaryButton {
                        text: "Start a new recording"
                        variant: "record"
                        iconName: "record"
                        hint: "⌘⇧R"
                        Layout.preferredWidth: 280
                        onClicked: root.app.newRecording()
                    }
                }
            }

            // Secondary action
            Rectangle {
                Layout.preferredWidth: 320
                Layout.preferredHeight: 236
                radius: 18
                color: Theme.raised
                border.width: 1
                border.color: Theme.stroke
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 28
                    spacing: 10
                    Rectangle {
                        width: 50
                        height: 50
                        radius: 15
                        color: Theme.accentSoft
                        Icon {
                            anchors.centerIn: parent
                            name: "folder"
                            size: 22
                            color: Theme.accent
                        }
                    }
                    Label {
                        text: "Open project"
                        color: Theme.text
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                    Item { Layout.fillHeight: true }
                    PrimaryButton {
                        text: "Open…"
                        variant: "ghost"
                        iconName: "folder"
                        Layout.fillWidth: true
                        onClicked: openDialog.open()
                    }
                }
            }
        }

        // Recent projects
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12
            FieldLabel { text: "Recent projects" }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 12
                rowSpacing: 12
                Repeater {
                    model: root.app.recentProjects
                    delegate: AbstractButton {
                        id: recent
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        hoverEnabled: true
                        onClicked: root.app.openProject(modelData.path)
                        background: Rectangle {
                            radius: Theme.radiusL
                            color: recent.hovered ? Theme.hover : Theme.raised
                            border.width: 1
                            border.color: recent.hovered ? Theme.strokeStrong : Theme.stroke
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            Rectangle {
                                width: 40
                                height: 40
                                radius: 10
                                color: Theme.accentSoft
                                Icon {
                                    anchors.centerIn: parent
                                    name: "screen"
                                    size: 18
                                    color: Theme.accent
                                }
                            }
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: recent.modelData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fontM
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: recent.modelData.modified
                                    color: Theme.textFaint
                                    font.pixelSize: Theme.fontXS
                                }
                            }
                        }
                        leftPadding: 14
                        rightPadding: 14
                    }
                }
            }
        }
    }

    Label {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 18
        text: root.app.platformName + "  ·  v" + root.app.version
        color: Theme.textFaint
        font.pixelSize: Theme.fontXS
    }
}
