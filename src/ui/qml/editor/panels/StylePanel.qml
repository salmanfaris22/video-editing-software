import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Text: titles, lower thirds, captions and callouts with presets.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback

    readonly property var sel: root.project.selection
    readonly property bool isText: root.sel.role === "text"
    readonly property var presets: [
        { id: "title", label: "Title", sample: "Big title" },
        { id: "lower-third", label: "Lower third", sample: "Name · Role" },
        { id: "caption", label: "Caption", sample: "A short caption" },
        { id: "callout", label: "Callout", sample: "Look here!" }
    ]
    readonly property var texts: {
        const out = []
        for (const track of root.project.tracks)
            for (const clip of track.clips)
                if (clip.role === "text") out.push(clip)
        return out
    }

    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Label { text: "Text"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }
        InputField {
            id: newText
            Layout.fillWidth: true
            placeholderText: "Type your text, then pick a style…"
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 8
            Repeater {
                model: root.presets
                delegate: PrimaryButton {
                    required property var modelData
                    Layout.fillWidth: true
                    variant: "ghost"
                    text: modelData.label
                    iconName: "plus"
                    onClicked: {
                        const value = newText.text.trim().length > 0 ? newText.text.trim() : modelData.sample
                        root.project.addText(value, root.playback.position, 4.0, modelData.id)
                        newText.text = ""
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.isText
            spacing: 8
            SectionLabel { text: "Selected text" }
            TextArea {
                id: editor
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                wrapMode: TextEdit.Wrap
                color: Theme.text
                font.pixelSize: Theme.fontM
                background: Rectangle {
                    radius: Theme.radiusM
                    color: Theme.raised
                    border.width: 1
                    border.color: editor.activeFocus ? Theme.accent : Theme.stroke
                }
                onTextChanged: if (activeFocus && root.isText && text !== root.sel.text) root.project.setText(root.project.selectedClip, text)
                Binding {
                    target: editor
                    property: "text"
                    value: root.sel.text || ""
                    when: !editor.activeFocus
                    restoreMode: Binding.RestoreNone
                }
            }
            Segmented {
                Layout.fillWidth: true
                options: root.presets.map(p => ({ label: p.label, value: p.id }))
                value: root.sel.preset || "title"
                onSelected: value => root.project.setTextValue(root.project.selectedClip, "preset", value)
            }
            SliderRow {
                label: "Size"
                from: 20; to: 200; stepSize: 1; defaultValue: 64
                value: root.sel.textSize || 64
                format: v => Math.round(v) + " px"
                onMoved: v => root.project.setTextValue(root.project.selectedClip, "size", Math.round(v))
            }
            FieldLabel { text: "Color" }
            ColorSwatches {
                Layout.fillWidth: true
                colors: ["#FFFFFFFF", "#FF15171C", "#FFF5C142", "#FF7C8CFF", "#FF34D399", "#FFFF4D5E"]
                current: root.sel.textColor || ""
                onPicked: color => root.project.setTextValue(root.project.selectedClip, "color", color)
            }
            FieldLabel { text: "Box" }
            ColorSwatches {
                Layout.fillWidth: true
                colors: ["", "#B3000000", "#E614161C", "#FFFFFFFF", "#FFF5C142", "#FF7C8CFF"]
                current: root.sel.textBackground || ""
                onPicked: color => root.project.setTextValue(root.project.selectedClip, "background", color)
            }
            Segmented {
                Layout.fillWidth: true
                options: [{ label: "Left", value: "left" }, { label: "Center", value: "center" }, { label: "Right", value: "right" }]
                value: root.sel.textAlignment || "center"
                onSelected: value => root.project.setTextValue(root.project.selectedClip, "alignment", value)
            }
            SliderRow {
                label: "Horizontal position"
                from: 0; to: 1; defaultValue: 0.5
                value: root.sel.x === undefined ? 0.5 : root.sel.x
                format: v => Math.round(v * 100) + "%"
                onMoved: v => root.project.setClipPosition(root.project.selectedClip, v, root.sel.y)
            }
            SliderRow {
                label: "Vertical position"
                from: 0; to: 1; defaultValue: 0.5
                value: root.sel.y === undefined ? 0.5 : root.sel.y
                format: v => Math.round(v * 100) + "%"
                onMoved: v => root.project.setClipPosition(root.project.selectedClip, root.sel.x, v)
            }
            TextAnimationSection {
                Layout.fillWidth: true
                project: root.project
                playback: root.playback
            }
            SliderRow {
                label: "Duration"
                from: 0.5; to: 30; defaultValue: 4
                value: root.sel.duration || 4
                format: v => v.toFixed(1) + " s"
                onMoved: v => root.project.setClipTiming(root.project.selectedClip, root.sel.start, v)
            }
            PrimaryButton {
                Layout.fillWidth: true
                variant: "danger"
                text: "Delete text"
                iconName: "trash"
                onClicked: root.project.deleteSelected()
            }
        }

        SectionLabel { text: root.texts.length > 0 ? "All text" : "" }
        Repeater {
            model: root.texts
            delegate: Rectangle {
                id: row
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 30
                radius: Theme.radiusS
                color: root.project.selectedClip === modelData.id ? Theme.accentSoft : rowMouse.containsMouse ? Theme.hover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    Icon { name: "text"; size: 14; color: Theme.textMuted }
                    Label { Layout.fillWidth: true; text: row.modelData.name; color: Theme.text; font.pixelSize: Theme.fontS; elide: Text.ElideRight }
                    Label { text: root.project.formatTime(row.modelData.start); color: Theme.textFaint; font.pixelSize: Theme.fontXS; font.family: Theme.monoFamily }
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.project.selectClip(row.modelData.id)
                        root.playback.seek(row.modelData.start + Math.min(0.5, row.modelData.duration / 2))
                    }
                }
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
