import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Project basics: title, canvas shape, frame rate and background.
ScrollView {
    id: root

    property ProjectController project
    property PlaybackController playback

    readonly property var solids: ["#0E0F13", "#1B1D24", "#F4F4F5", "#1E3A8A", "#14532D", "#7C2D12", "#4C1D95", "#0F766E"]
    readonly property var gradients: [["#3A1C71", "#D76D77"], ["#0F2027", "#2C5364"], ["#1D2B64", "#F8CDDA"],
                                      ["#134E5E", "#71B280"], ["#232526", "#414345"], ["#FF7E5F", "#FEB47B"]]

    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: root.availableWidth
        spacing: 10

        Label { text: "Setup"; color: Theme.text; font.pixelSize: Theme.fontL; font.weight: Font.DemiBold }

        SectionLabel { text: "Title" }
        InputField {
            Layout.fillWidth: true
            text: root.project.title
            onEditingFinished: if (text.trim() !== root.project.title) root.project.setTitle(text)
        }

        SectionLabel { text: "Canvas" }
        Segmented {
            Layout.fillWidth: true
            options: [{ label: "16:9", value: "16:9" }, { label: "9:16", value: "9:16" },
                      { label: "1:1", value: "1:1" }, { label: "4:5", value: "4:5" }]
            value: root.project.aspect
            onSelected: value => root.project.setCanvasAspect(value)
        }
        SectionLabel { text: "Frame rate" }
        Segmented {
            Layout.fillWidth: true
            options: [{ label: "24", value: 24 }, { label: "25", value: 25 }, { label: "30", value: 30 },
                      { label: "50", value: 50 }, { label: "60", value: 60 }]
            value: Math.round(root.project.frameRate)
            onSelected: value => root.project.setFrameRate(value)
        }

        SectionLabel { text: "Background" }
        ColorSwatches {
            Layout.fillWidth: true
            colors: root.solids
            current: root.project.backgroundColor2.length === 0 ? root.project.backgroundColor : ""
            onPicked: color => root.project.setBackground(color, "")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: root.gradients
                delegate: Rectangle {
                    id: swatch
                    required property var modelData
                    readonly property bool current: root.project.backgroundColor.toLowerCase() === modelData[0].toLowerCase()
                                                    && root.project.backgroundColor2.toLowerCase() === modelData[1].toLowerCase()
                    width: 44
                    height: 26
                    radius: 6
                    border.width: current ? 2 : 1
                    border.color: current ? Theme.accent : Theme.strokeStrong
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: swatch.modelData[0] }
                        GradientStop { position: 1; color: swatch.modelData[1] }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.project.setBackground(swatch.modelData[0], swatch.modelData[1])
                    }
                }
            }
        }
        SectionLabel { text: "Project" }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 4
            Label { text: "Length"; color: Theme.textMuted; font.pixelSize: Theme.fontS }
            Label { text: root.project.formatTime(root.project.duration); color: Theme.text; font.pixelSize: Theme.fontS }
            Label { text: "Tracks"; color: Theme.textMuted; font.pixelSize: Theme.fontS }
            Label { text: root.project.tracks.length; color: Theme.text; font.pixelSize: Theme.fontS }
            Label { text: "Saved"; color: Theme.textMuted; font.pixelSize: Theme.fontS }
            Label {
                text: root.project.dirty ? "Saving…" : "All changes saved"
                color: root.project.dirty ? Theme.warning : Theme.success
                font.pixelSize: Theme.fontS
            }
        }
        Item { Layout.preferredHeight: 8 }
    }
}
