import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    required property AppController app
    readonly property RecorderController rec: app.recorder
    readonly property bool idle: rec.state === "idle"
    readonly property bool phoneCamera: rec.cameraId === "phone"
    readonly property var selectedScreen: {
        for (let i = 0; i < rec.screenTargets.length; ++i)
            if (rec.screenTargets[i].id === rec.screenTarget) return rec.screenTargets[i]
        return null
    }
    readonly property real screenAspect: {
        if (!selectedScreen || !selectedScreen.detail) return 16 / 10
        const m = String(selectedScreen.detail).match(/(\d+)\s*[×x]\s*(\d+)/)
        return m ? parseInt(m[1], 10) / parseInt(m[2], 10) : 16 / 10
    }

    // ---- Permission notice (inline component) --------------------------------
    component PermissionNotice: Rectangle {
        id: notice
        property string kind
        property string status
        property string label
        visible: status !== "granted" && status !== "not-applicable" && status.length > 0
        Layout.fillWidth: true
        implicitHeight: noticeRow.implicitHeight + 16
        radius: 0
        color: Theme.warningSoft
        border.width: 1
        border.color: Qt.rgba(0.96, 0.72, 0.24, 0.35)
        RowLayout {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: 8
            anchors.leftMargin: 12
            spacing: 8
            Icon { name: "warning"; size: 14; color: Theme.warning }
            readonly property bool restart: notice.kind === "screen" && root.rec.screenRestartNeeded
            Label {
                Layout.fillWidth: true
                text: noticeRow.restart
                      ? "Turn on Lectern in Privacy & Security › Screen & System Audio Recording, then restart Lectern."
                      : notice.label + (notice.status === "denied" ? " access is turned off" : " access is needed")
                color: Theme.text
                font.pixelSize: Theme.fontS
                wrapMode: Text.WordWrap
            }
            PrimaryButton {
                implicitHeight: 28
                variant: "ghost"
                text: notice.status === "denied" || noticeRow.restart ? "Settings" : "Allow"
                onClicked: notice.status === "denied" || noticeRow.restart ? root.rec.openPrivacySettings(notice.kind)
                                                                           : root.rec.requestPermission(notice.kind)
            }
            PrimaryButton {
                visible: noticeRow.restart
                implicitHeight: 28
                text: "Restart"
                onClicked: root.rec.restartApp()
            }
        }
    }

    // ---- Header ----------------------------------------------------------------
    RowLayout {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        height: 40
        spacing: 10
        IconButton {
            iconName: "chevron-left"
            tooltip: "Back"
            enabled: root.idle
            onClicked: root.app.goHome()
        }
        Label {
            text: "New recording"
            color: Theme.text
            font.pixelSize: 17
            font.weight: Font.DemiBold
        }
        Item { Layout.fillWidth: true }
        IconButton {
            iconName: "setup"
            tooltip: "Refresh sources"
            enabled: root.idle && !root.rec.loadingSources
            onClicked: root.rec.refreshSources()
        }
    }

    RowLayout {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        anchors.topMargin: 12
        spacing: 20

        // ---- Stage: what will be recorded ------------------------------------
        Rectangle {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 0
            color: Theme.surface
            border.width: 1
            border.color: Theme.stroke

            Rectangle {
                id: screenFrame
                readonly property real aspect: root.screenAspect
                width: Math.min(parent.width - 48, (parent.height - 100) * aspect)
                height: width / aspect
                anchors.centerIn: parent
                anchors.verticalCenterOffset: -8
                radius: 0
                color: "#0D0F14"
                clip: true
                border.width: 1
                border.color: Theme.strokeStrong

                VideoPreviewItem {
                    id: screenPreview
                    anchors.fill: parent
                    visible: root.rec.screenTarget.length > 0
                    source: root.rec.screenPreview
                    fill: true
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    visible: root.rec.screenTarget.length > 0 && !screenPreview.hasFrame
                    Icon {
                        Layout.alignment: Qt.AlignHCenter
                        name: root.selectedScreen && root.selectedScreen.kind === "window" ? "window"
                            : root.selectedScreen && root.selectedScreen.kind === "app" ? "app" : "screen"
                        size: 40
                        color: Theme.textFaint
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.selectedScreen ? root.selectedScreen.name : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontL
                        font.weight: Font.DemiBold
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.rec.screenStatus === "No access"
                              ? "Screen recording access needed"
                              : (root.selectedScreen ? root.selectedScreen.detail : "Starting preview…")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontS
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: root.rec.screenTarget.length === 0
                    text: "No screen — camera and audio only"
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontM
                }

                // Camera bubble with the live preview.
                Rectangle {
                    id: bubble
                    visible: root.rec.cameraId.length > 0
                    width: Math.max(180, parent.width * 0.28)
                    height: width * 9 / 16
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    radius: 0
                    color: "#000000"
                    clip: true
                    border.width: 2
                    border.color: cameraPreviewItem.hasFrame ? Qt.rgba(1, 1, 1, 0.18) : Theme.strokeStrong
                    z: 1

                    VideoPreviewItem {
                        id: cameraPreviewItem
                        anchors.fill: parent
                        source: root.phoneCamera ? root.rec.phonePreview : root.rec.cameraPreview
                        mirrored: !root.phoneCamera
                        fill: true
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: !cameraPreviewItem.hasFrame
                        text: root.phoneCamera
                              ? (root.rec.phoneUrl.length ? "Open the share link on your phone…" : "Starting link…")
                              : (root.rec.cameraStatus === "No access" ? "Camera access needed" : "Starting camera…")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontS
                        horizontalAlignment: Text.AlignHCenter
                        width: parent.width - 16
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Row {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 18
                spacing: 8
                StatusChip {
                    visible: root.rec.screenTarget.length > 0
                    text: "Screen · " + root.rec.screenStatus
                    tone: root.rec.screenStatus === "Live" ? "ok"
                        : root.rec.screenStatus === "Off" ? "off"
                        : root.rec.screenStatus.indexOf("Starting") === 0 ? "info" : "warn"
                }
                StatusChip {
                    visible: root.rec.cameraId.length > 0 && !root.phoneCamera
                    text: "Camera · " + root.rec.cameraStatus
                    tone: root.rec.cameraStatus === "Live" ? "ok"
                        : root.rec.cameraStatus === "Off" ? "off"
                        : root.rec.cameraStatus.indexOf("Starting") === 0 ? "info" : "warn"
                }
                StatusChip {
                    visible: root.phoneCamera
                    text: "Phone · " + root.rec.phoneStatus
                    tone: root.rec.phoneStatus === "Streaming" ? "ok"
                        : root.rec.phoneStatus.indexOf("Waiting") === 0 ? "info"
                        : root.rec.phoneStatus === "Idle" ? "off" : "warn"
                }
                StatusChip {
                    text: "Mic · " + root.rec.micStatus
                    tone: root.rec.micStatus === "Live" ? "ok" : root.rec.micStatus === "Off" ? "off" : "warn"
                }
                StatusChip {
                    visible: root.rec.systemAudio && root.rec.systemAudioSupported
                    text: "System audio · On"
                    tone: "info"
                }
            }
        }

        // ---- Settings column ----------------------------------------------------
        ScrollView {
            Layout.preferredWidth: 380
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 18

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel { text: "Screen" }
                    SourcePicker {
                        Layout.fillWidth: true
                        enabled: root.idle
                        sources: root.rec.screenTargets
                        currentId: root.rec.screenTarget
                        noneText: "Don't record the screen"
                        iconName: "screen"
                        onPicked: id => root.rec.screenTarget = id
                    }
                    PermissionNotice { kind: "screen"; status: root.rec.screenPermission; label: "Screen recording" }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel { text: "Camera" }
                    SourcePicker {
                        Layout.fillWidth: true
                        enabled: root.idle
                        sources: root.rec.cameras
                        currentId: root.rec.cameraId
                        noneText: "No camera"
                        iconName: "camera"
                        onPicked: id => root.rec.cameraId = id
                    }
                    PermissionNotice {
                        kind: "camera"
                        status: root.phoneCamera ? "not-applicable" : root.rec.cameraPermission
                        label: "Camera"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: root.phoneCamera
                    FieldLabel { text: "Share link" }
                    ShareLinkPicker {
                        Layout.fillWidth: true
                        enabled: root.idle
                        url: root.rec.phoneUrl
                        statusText: root.rec.phoneStatus
                        onCopyRequested: root.rec.copyPhoneUrl()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel { text: "Microphone" }
                    SourcePicker {
                        Layout.fillWidth: true
                        enabled: root.idle
                        sources: root.rec.microphones
                        currentId: root.rec.microphoneId
                        noneText: "No microphone"
                        iconName: "mic"
                        onPicked: id => root.rec.microphoneId = id
                    }
                    LevelMeter {
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        level: root.rec.micLevel
                        clipping: root.rec.micClipping
                        active: root.rec.microphoneId.length > 0
                    }
                    PermissionNotice { kind: "microphone"; status: root.rec.microphonePermission; label: "Microphone" }
                }

                ToggleRow {
                    Layout.fillWidth: true
                    iconName: "speaker"
                    title: "System audio"
                    subtitle: root.rec.systemAudioSupported ? "Sound from apps on this computer" : "Not supported on this system"
                    checked: root.rec.systemAudio
                    enabled: root.idle && root.rec.systemAudioSupported
                    onToggled: checked => root.rec.systemAudio = checked
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.stroke }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FieldLabel { text: "Quality" }
                    Segmented {
                        Layout.fillWidth: true
                        enabled: root.idle
                        options: [{ label: "720p", value: "720p" }, { label: "1080p", value: "1080p" },
                                  { label: "1440p", value: "1440p" }, { label: "4K", value: "2160p" }]
                        value: root.rec.resolution
                        onSelected: v => root.rec.resolution = v
                    }
                    Segmented {
                        Layout.fillWidth: true
                        enabled: root.idle
                        options: [{ label: "24 fps", value: 24 }, { label: "30 fps", value: 30 }, { label: "60 fps", value: 60 }]
                        value: root.rec.frameRate
                        onSelected: v => root.rec.frameRate = v
                    }
                }

                ToggleRow {
                    Layout.fillWidth: true
                    iconName: "timer"
                    title: "Countdown"
                    subtitle: "3 seconds before recording starts"
                    checked: root.rec.countdownEnabled
                    onToggled: checked => root.rec.countdownEnabled = checked
                }

                Banner {
                    Layout.fillWidth: true
                    text: root.rec.errorMessage
                    tone: "error"
                    onDismissed: root.rec.dismissError()
                }
                Repeater {
                    model: root.rec.warnings
                    delegate: Label {
                        required property string modelData
                        Layout.fillWidth: true
                        text: "• " + modelData
                        color: Theme.warning
                        font.pixelSize: Theme.fontS
                        wrapMode: Text.WordWrap
                    }
                }

                PrimaryButton {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    implicitHeight: 48
                    variant: "record"
                    iconName: "record"
                    hint: "⌘⇧R"
                    text: root.rec.state === "preparing" ? "Preparing…" : "Start recording"
                    enabled: root.idle && (root.rec.screenTarget.length > 0 || root.rec.cameraId.length > 0
                                           || root.rec.microphoneId.length > 0)
                    onClicked: root.rec.startRecording()
                }
            }
        }
    }
}
