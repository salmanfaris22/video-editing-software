#pragma once

// What the canvas shows at one instant (docs/RENDERING_PIPELINE.md §3): a
// flat, resolution-independent list of layers derived from the project.
// Pure data — the compositor draws it, tests inspect it.

#include "editor/LayoutPresets.h"
#include "project/Project.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lectern::editor {

enum class LayerKind { Media, Text, Subtitle };

struct ColorParams {
    using Wheel = timeline::ColorAdjustments::Wheel;
    double exposure = 0;     ///< stops, −2…2
    double brightness = 0;   ///< −1…1
    double contrast = 0;     ///< −1…1
    double saturation = 0;   ///< −1…1 (−1 = grayscale)
    double temperature = 0;  ///< −1 (cool) … 1 (warm)
    double tint = 0;         ///< −1 (green) … 1 (magenta)
    Wheel lift;              ///< color wheels (shadows, midtones, highlights)
    Wheel gamma;
    Wheel gain;
    Wheel offset;            ///< whole-signal shift (Resolve Offset)
    double pivot = 0.5;      ///< contrast pivot
    double shadows = 0;      ///< −1…1, darks only
    double highlights = 0;   ///< −1…1, brights only
    double colorBoost = 0;   ///< −1…1 vibrance
    double hue = 0;          ///< −1…1 = −180°…180°
    std::array<std::vector<timeline::Vec2>, 4> curves;  ///< custom curves Y, R, G, B (control points)
    std::string lut;         ///< .cube path relative to the project, or "builtin:<id>"
    double lutAmount = 1.0;
    /// Everything except the LUT is a per-channel curve plus saturation.
    [[nodiscard]] bool curvesAreIdentity() const noexcept {
        return exposure == 0 && brightness == 0 && contrast == 0 && saturation == 0 && temperature == 0 && tint == 0 &&
               lift.isIdentity() && gamma.isIdentity() && gain.isIdentity() && offset.isIdentity() && shadows == 0 &&
               highlights == 0 && colorBoost == 0 && hue == 0 && curves[0].empty() && curves[1].empty() &&
               curves[2].empty() && curves[3].empty();
    }
    [[nodiscard]] bool isIdentity() const noexcept { return curvesAreIdentity() && (lut.empty() || lutAmount <= 0); }
    friend bool operator==(const ColorParams&, const ColorParams&) = default;
};

/// How a source's pixel values are to be read (Phase 1.2 color management).
/// The working space is display-referred Rec.709 (BT.1886); everything else
/// is converted into it: wide gamuts by matrix, HDR (PQ, HLG) by tone mapping.
enum class Transfer { Bt709, Srgb, Pq, Hlg };
enum class Primaries { Bt709, DisplayP3, Bt2020 };
struct InputColor {
    Transfer transfer = Transfer::Bt709;
    Primaries primaries = Primaries::Bt709;
    /// Rec.709 / sRGB content needs no conversion (the pre-1.2 behaviour).
    [[nodiscard]] bool isIdentity() const noexcept {
        return primaries == Primaries::Bt709 && (transfer == Transfer::Bt709 || transfer == Transfer::Srgb);
    }
    friend bool operator==(const InputColor&, const InputColor&) = default;
};
/// From a clip's override ("auto", "rec709", "srgb", "display-p3", "rec2020",
/// "rec2020-hlg", "rec2020-pq") and the file's FFmpeg color tags.
[[nodiscard]] InputColor resolveInputColor(std::string_view override, std::string_view transferTag,
                                           std::string_view primariesTag);

/// A node (secondary correction after the primary grade) as the renderers
/// see it: its grade and what limits it (window × qualifier × subject).
struct NodeParams {
    ColorParams grade;  ///< no LUT
    timeline::ColorAdjustments::Window window;
    timeline::ColorAdjustments::Qualifier qualifier;
    int subject = 0;     ///< 0 whole picture, 1 person, 2 background
    bool invert = false;
    friend bool operator==(const NodeParams&, const NodeParams&) = default;
};

struct VisualLayer {
    LayerKind kind = LayerKind::Media;
    timeline::ClipId clip;
    std::string role;  ///< "screen", "camera", "overlay", "text", "subtitle"

    // Media
    project::MediaId media;
    project::MediaKind mediaKind = project::MediaKind::Video;
    Time sourceTime;
    NormRect box;          ///< destination on the canvas
    bool fill = false;     ///< crop to fill the box (camera) instead of fitting inside (screen)
    double radius = 0;     ///< corner radius, fraction of canvas height
    bool circle = false;
    double opacity = 1;
    double shadow = 0;     ///< 0…1
    double border = 0;     ///< fraction of canvas height
    std::string borderColor = "#FFFFFF";
    bool mirror = false;
    double zoom = 1;       ///< ≥ 1: magnify around (zoomX, zoomY) of the source
    double zoomX = 0.5;
    double zoomY = 0.5;
    double rotation = 0;   ///< degrees clockwise
    double cropL = 0;      ///< source trim, fractions 0…1
    double cropT = 0;
    double cropR = 0;
    double cropB = 0;
    double blur = 0;       ///< 0…1
    double vignette = 0;   ///< 0…1
    double backgroundBlur = 0;  ///< 0…1: blur behind the person (needs a segmenter)
    double sourceAspect = 0;    ///< media width / height (0 = unknown)
    ColorParams color;
    InputColor input;  ///< how to read the source's colors
    std::vector<NodeParams> nodes;  ///< after `color`, in order (at most ColorAdjustments::kMaxNodes)
    int highlightNode = -1;         ///< ≥ 0: show that node's selection in color over gray (Color page)

    // Text / subtitle
    std::string text;
    timeline::TextStyle textStyle;
    std::string textPreset;
    double anchorX = 0.5;  ///< text center, canvas fractions
    double anchorY = 0.5;
    // Text animation state at this instant (in/out animations).
    double textDx = 0;      ///< offset, canvas fractions
    double textDy = 0;
    double textScale = 1;   ///< about the block center
    double textReveal = 1;  ///< wipe: visible fraction from the left
    double textBlur = 0;    ///< blur radius, fraction of the canvas height
    int textChars = -1;     ///< typewriter: characters shown (−1 = all)
};

struct RenderPlan {
    int width = 1920;   ///< canvas size the fractions refer to
    int height = 1080;
    std::string layout;  ///< the screen/camera layout preset in effect
    std::string background = "#0E0F13";
    std::string background2;  ///< gradient end; empty = solid
    project::StyleSettings style;
    std::vector<VisualLayer> layers;  ///< bottom → top
};

/// The plan for timeline time `t`.
[[nodiscard]] RenderPlan buildRenderPlan(const project::Project& project, Time t);
/// The plan with every grade removed (before/after compare): layers keep
/// their input color conversion, effects and placement.
[[nodiscard]] RenderPlan ungraded(RenderPlan plan);
/// Position of node `nodeId` among the clip's enabled nodes (what
/// VisualLayer::nodes holds), or −1.
[[nodiscard]] int enabledNodeIndex(const timeline::ColorAdjustments& color, std::string_view nodeId);
/// The enabled nodes of a clip's grade, as renderer parameters.
[[nodiscard]] std::vector<NodeParams> gradeNodes(const timeline::ColorAdjustments& color);
/// A clip's grade at clip-local time `local` as renderer parameters: the
/// correction, then (unless `withLook` is false) its creative look on top.
[[nodiscard]] ColorParams gradeAt(const timeline::ColorAdjustments& color, Time local, bool withLook = true);

/// Effect type ids understood by the compositor (clip.effects[].type).
inline constexpr const char* kEffectBlur = "lectern.blur";          ///< params: amount
inline constexpr const char* kEffectVignette = "lectern.vignette";  ///< params: amount
inline constexpr const char* kEffectZoom = "lectern.zoom";          ///< params: scale, x, y
inline constexpr const char* kEffectBackgroundBlur = "lectern.background-blur";  ///< params: amount

/// Layout slots for `preset` with the project's hand-placed slots for the
/// canvas aspect applied (RenderPlan and the canvas tools share this).
[[nodiscard]] LayoutSlots customizedSlots(const project::Project& project, std::string_view preset);

/// Text animation kinds in UI order: {id, name}.
[[nodiscard]] const std::vector<std::pair<std::string, std::string>>& textAnimations();

/// Default placement of text presets ("title", "lower-third", "caption", "callout").
struct TextPresetDefaults {
    double x;
    double y;
    timeline::TextStyle style;
    timeline::TextAnimation animation;  ///< for new text clips
};
[[nodiscard]] TextPresetDefaults textPresetDefaults(std::string_view preset);

}  // namespace lectern::editor
