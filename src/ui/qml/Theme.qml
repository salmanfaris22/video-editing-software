pragma Singleton
import QtQuick

// Design tokens for the whole UI, in two modes (dark and light) in the style
// of modern creative tools: soft graphite or paper surfaces, rounded cards,
// one royal-blue accent and one semantic "recording" red. Video surfaces
// (the viewer, curves, wheels, scopes) stay dark in both modes, as in
// Premiere Pro and Final Cut Pro, so colors are judged against black.
QtObject {
    id: theme

    /// "dark" or "light" (Main.qml restores the saved choice; "system" follows the OS).
    property string mode: "dark"
    property string preference: "dark"   // what the user chose: dark | light | system
    readonly property bool dark: mode !== "light"

    // Surfaces (darkest → lightest in dark mode)
    readonly property color bg: dark ? "#0E0F12" : "#E8EBF0"         // window chrome, gaps between panels
    readonly property color surface: dark ? "#15161A" : "#F6F7F9"    // panels
    readonly property color raised: dark ? "#1C1D22" : "#FFFFFF"     // cards on panels
    readonly property color inset: dark ? "#101115" : "#EEF0F4"      // fields, sunken areas
    readonly property color hover: dark ? "#23252B" : "#E9ECF2"
    readonly property color pressed: dark ? "#2B2E36" : "#DDE2EA"
    readonly property color selected: dark ? "#232A44" : "#E2E8FF"   // a chosen item (blue tint)
    readonly property color stroke: dark ? "#25272D" : "#DCE0E7"
    readonly property color strokeStrong: dark ? "#33363F" : "#C5CAD4"

    // Video surfaces: dark in both modes.
    readonly property color well: dark ? "#0B0C0F" : "#17191E"
    readonly property color wellStroke: dark ? "#262A33" : "#2B2F38"
    readonly property color knob: "#F2F3F6"                          // handles drawn over video

    // Text
    readonly property color text: dark ? "#F3F4F6" : "#15171C"
    readonly property color textSecondary: dark ? "#C9CCD3" : "#343843"
    readonly property color textMuted: dark ? "#9A9EA9" : "#5F6573"
    readonly property color textFaint: dark ? "#676C78" : "#8B91A0"
    readonly property color accentText: "#FFFFFF"

    // Accents
    readonly property color accent: "#3E5BF6"
    readonly property color accentHover: dark ? "#5672FF" : "#2E4AE6"
    readonly property color accentSoft: dark ? "#1E2752" : "#E3E8FF"
    readonly property color record: dark ? "#FF4D5E" : "#E5394A"
    readonly property color recordHover: dark ? "#FF6878" : "#F04E5E"
    readonly property color recordSoft: dark ? "#3A1A20" : "#FDE3E6"
    readonly property color success: dark ? "#34D399" : "#12A37A"
    readonly property color warning: dark ? "#F5B83D" : "#C98A0E"
    readonly property color warningSoft: dark ? "#3A2E14" : "#FCF0D6"
    readonly property color danger: dark ? "#F87171" : "#DC3D3D"

    // Shape & rhythm (rounded, like the reference design)
    readonly property int radiusS: 6
    readonly property int radiusM: 10
    readonly property int radiusL: 14
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

    /// Applies a preference: "dark", "light" or "system" (follows the OS appearance).
    function apply(choice) {
        theme.preference = choice === "light" || choice === "system" ? choice : "dark"
        if (theme.preference === "system")
            theme.mode = Qt.styleHints.colorScheme === Qt.ColorScheme.Light ? "light" : "dark"
        else
            theme.mode = theme.preference
    }

    property Connections systemScheme: Connections {
        target: Qt.styleHints
        function onColorSchemeChanged() { if (theme.preference === "system") theme.apply("system") }
    }

    function icon(name) {
        return "qrc:/qt/qml/Lectern/UI/icons/" + name + ".svg"
    }
}
