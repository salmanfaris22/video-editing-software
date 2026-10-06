import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

// Subtitles: write them at the playhead, import/export SRT or WebVTT, style them.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback

    readonly property var sel: root.project.selection
    readonly property bool isSubtitle: root.sel.role === "subtitle"

    contentWidth: availableWidth
    clip: true

    FileDialog {
        id: importDialog
        title: "Import subtitles"
        nameFilters: ["Subtitles (*.srt *.vtt)"]
        onAccepted: root.project.importSubtitles(selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: "Export subtitles"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "srt"
        nameFilters: ["SubRip (*.srt)", "WebVTT (*.vtt)"]
        onAccepted: root.project.exportSubtitles(selectedFile)
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Label { text: "Subtitles"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }
        InputField {
            id: line
            Layout.fillWidth: true
            placeholderText: "What is said at the playhead…"
            onAccepted: add.clicked()
        }
        SliderRow {
            id: length
            label: "Shown for"
            from: 0.5; to: 8; defaultValue: 3
            value: 3
            format: v => v.toFixed(1) + " s"
            onMoved: v => value = v
        }
        PrimaryButton {
            id: add
            Layout.fillWidth: true
            text: "Add subtitle at " + root.project.formatTime(root.playback.position)
            iconName: "plus"
            enabled: line.text.trim().length > 0
            onClicked: {
                root.project.addSubtitle(line.text, root.playback.position, length.value)
                root.playback.seek(root.playback.position + length.value)
                line.text = ""
                line.forceActiveFocus()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            PrimaryButton {
                Layout.fillWidth: true
                variant: "ghost"
                text: "Import…"
                iconName: "upload"
                onClicked: importDialog.open()
            }
            PrimaryButton {
                Layout.fillWidth: true
                variant: "ghost"
                text: "Export…"
                iconName: "download"
                enabled: root.project.subtitles.length > 0
                onClicked: exportDialog.open()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.isSubtitle
            spacing: 8
            SectionLabel { text: "Selected subtitle" }
            TextArea {
                id: editor
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                wrapMode: TextEdit.Wrap
                color: Theme.text
                font.pixelSize: Theme.fontM
                background: Rectangle {
                    radius: Theme.radiusM
                    color: Theme.raised
                    border.width: 1
                    border.color: editor.activeFocus ? Theme.accent : Theme.stroke
                }
                onTextChanged: if (activeFocus && root.isSubtitle && text !== root.sel.text) root.project.setSubtitleText(root.project.selectedClip, text)
                Binding {
                    target: editor
                    property: "text"
                    value: root.sel.text || ""
                    when: !editor.activeFocus
                    restoreMode: Binding.RestoreNone
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: root.project.formatTime(root.sel.start || 0) + " · " + (root.sel.duration || 0).toFixed(1) + " s"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontXS
                }
                PrimaryButton {
                    variant: "danger"
                    text: "Delete"
                    iconName: "trash"
                    onClicked: root.project.deleteSelected()
                }
            }
        }

        SectionLabel { text: "Look" }
        SliderRow {
            label: "Size"
            from: 0.025; to: 0.08; defaultValue: 0.045
            value: root.project.style.subtitleSize || 0.045
            format: v => Math.round(v * 1000) / 10 + "%"
            onMoved: v => root.project.setStyleValue("subtitleSize", v)
        }
        SliderRow {
            label: "Position"
            from: 0.5; to: 0.95; defaultValue: 0.88
            value: root.project.style.subtitlePosition || 0.88
            format: v => Math.round(v * 100) + "%"
            onMoved: v => root.project.setStyleValue("subtitlePosition", v)
        }
        FieldLabel { text: "Text" }
        ColorSwatches {
            Layout.fillWidth: true
            colors: ["#FFFFFF", "#FFF5C142", "#FF15171C"]
            current: root.project.style.subtitleColor || ""
            onPicked: color => root.project.setStyleValue("subtitleColor", color)
        }
        FieldLabel { text: "Box" }
        ColorSwatches {
            Layout.fillWidth: true
            colors: ["", "#B3000000", "#80000000", "#FFFFFFFF"]
            current: root.project.style.subtitleBackground || ""
            onPicked: color => root.project.setStyleValue("subtitleBackground", color)
        }

        SectionLabel { text: root.project.subtitles.length + " subtitles" }
        Repeater {
            model: root.project.subtitles
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool playing: root.playback.position >= modelData.start && root.playback.position < modelData.start + modelData.duration
                Layout.fillWidth: true
                implicitHeight: Math.max(30, cueText.implicitHeight + 10)
                radius: Theme.radiusS
                color: root.project.selectedClip === modelData.id ? Theme.accentSoft : playing ? Theme.raised : rowMouse.containsMouse ? Theme.hover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    Label {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 6
                        text: root.project.formatTime(row.modelData.start)
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontXS
                        font.family: Theme.monoFamily
                    }
                    Label {
                        id: cueText
                        Layout.fillWidth: true
                        text: row.modelData.text
                        color: Theme.text
                        font.pixelSize: Theme.fontS
                        wrapMode: Text.WordWrap
                    }
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.project.selectClip(row.modelData.id)
                        root.playback.seek(row.modelData.start)
                    }
                }
            }
        }
        PrimaryButton {
            Layout.fillWidth: true
            visible: root.project.subtitles.length > 0
            variant: "ghost"
            text: "Remove all subtitles"
            onClicked: root.project.clearSubtitles()
        }
        Item { Layout.preferredHeight: 8 }
    }
}
