pragma Singleton
import QtQuick

// Design tokens for the whole UI: a dark, calm editor palette with one
// accent (periwinkle) and one semantic "recording" red.
QtObject {
    // Surfaces (darkest → lightest)
    readonly property color bg: "#0B0C10"
    readonly property color surface: "#111318"
    readonly property color raised: "#171A21"
    readonly property color hover: "#1E222B"
    readonly property color pressed: "#262B36"
    readonly property color stroke: "#232731"
    readonly property color strokeStrong: "#313645"

    // Text
    readonly property color text: "#ECEEF3"
    readonly property color textMuted: "#9097A6"
    readonly property color textFaint: "#5B6271"

    // Accents
    readonly property color accent: "#7C8CFF"
    readonly property color accentHover: "#909EFF"
    readonly property color accentSoft: "#20264A"
    readonly property color record: "#FF4D5E"
    readonly property color recordHover: "#FF6878"
    readonly property color recordSoft: "#3A1A20"
    readonly property color success: "#34D399"
    readonly property color warning: "#F5B83D"
    readonly property color warningSoft: "#3A2E14"
    readonly property color danger: "#F87171"

    // Shape & rhythm
    readonly property int radiusS: 0
    readonly property int radiusM: 0
    readonly property int radiusL: 0
    readonly property int gapS: 6
    readonly property int gap: 10
    readonly property int pad: 16
    readonly property int padL: 24

    // Type scale (pixels)
    readonly property int fontXS: 11
    readonly property int fontS: 12
    readonly property int fontM: 13
    readonly property int fontL: 15
    readonly property int fontXL: 20
    readonly property int fontXXL: 30
    readonly property string monoFamily: Qt.platform.os === "osx" ? "Menlo" : "monospace"

    function icon(name) {
        return "qrc:/qt/qml/Lectern/UI/icons/" + name + ".svg"
    }
}
