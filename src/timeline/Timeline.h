#pragma once

#include "core/Error.h"
#include "core/Time.h"
#include "core/Uuid.h"
#include "timeline/Animated.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <array>
#include <vector>

namespace lectern::timeline {

using ClipId = Id<struct ClipTag>;
using TrackId = Id<struct TrackTag>;
using MediaId = Id<struct MediaTag>;
using MarkerId = Id<struct MarkerTag>;
using LinkGroupId = Id<struct LinkGroupTag>;
using EffectId = Id<struct EffectTag>;
using LayoutRegionId = Id<struct LayoutRegionTag>;

enum class FitMode { Fit, Fill, Stretch, None };

/// Clip placement on the canvas. Position is the anchor point in normalized
/// canvas coordinates, so layouts survive canvas resolution changes.
struct Transform {
    FitMode fit = FitMode::Fit;
    Animated<Vec2> position{Vec2{0.5, 0.5}};
    Animated<Vec2> scale{Vec2{1.0, 1.0}};
    Animated<double> rotation{0.0};  ///< degrees, clockwise
    Animated<Vec2> anchor{Vec2{0.5, 0.5}};  ///< transform origin, normalized
    bool flipH = false;
    bool flipV = false;
    friend bool operator==(const Transform&, const Transform&) = default;
};

struct AudioProperties {
    double gainDb = 0.0;
    Animated<double> volume{1.0};
    Time fadeIn;
    Time fadeOut;
    bool muted = false;
    friend bool operator==(const AudioProperties&, const AudioProperties&) = default;
};

/// The HSL curves of a grade (index into ColorAdjustments::hslCurves):
/// x axis → what the curve changes.
enum HslCurve : int {
    kHueVsHue = 0,  ///< hue → hue shift (y 0…1 = −180°…+180°)
    kHueVsSat = 1,  ///< hue → saturation (y 0…1 = ×0…×2)
    kHueVsLum = 2,  ///< hue → luminance (y 0…1 = −0.5…+0.5, weighted by saturation)
    kLumVsSat = 3,  ///< luminance → saturation
    kSatVsSat = 4,  ///< saturation → saturation
    kSatVsLum = 5,  ///< saturation → luminance
};

struct ColorAdjustments {
    Animated<double> exposure{0.0};
    Animated<double> brightness{0.0};
    Animated<double> contrast{0.0};
    Animated<double> highlights{0.0};
    Animated<double> shadows{0.0};
    Animated<double> saturation{0.0};
    Animated<double> temperature{0.0};
    Animated<double> tint{0.0};
    Animated<double> gamma{1.0};
    Animated<double> sharpness{0.0};
    /// Primary color wheels and LUT (docs/RENDERING_PIPELINE.md §4.3).
    struct Wheel {
        double x = 0;       ///< hue push on the vectorscope plane (Cb), −1…1
        double y = 0;       ///< (Cr), −1…1; |(x, y)| ≤ 1
        double master = 0;  ///< luma, −1…1
        [[nodiscard]] bool isIdentity() const noexcept { return x == 0 && y == 0 && master == 0; }
        friend bool operator==(const Wheel&, const Wheel&) = default;
    };
    Wheel lift;   ///< shadows
    Wheel gammaWheel;  ///< midtones
    Wheel gain;   ///< highlights
    Wheel offset;  ///< the whole signal (Resolve's Offset wheel)
    double pivot = 0.5;  ///< contrast turns around this level (0…1)
    Animated<double> colorBoost{0.0};  ///< vibrance: saturates muted colors more (−1…1)
    Animated<double> hue{0.0};         ///< hue rotation, −1…1 = −180°…180°
    /// Custom curves (Resolve "Curves – Custom"): control points (in, out) in
    /// 0…1 for luma (applied to all channels first) and R, G, B. Fewer than
    /// two points, or exactly the diagonal, means no curve.
    std::array<std::vector<Vec2>, 4> curves;  ///< Y, R, G, B
    /// Resolve's HSL curves, in kHslCurve order: points (x, y) in 0…1, 0.5 =
    /// no change. Hue curves wrap around (x = hue, 0 = red); fewer than two
    /// points or a flat line at 0.5 means no curve.
    std::array<std::vector<Vec2>, 6> hslCurves;
    /// A 3D LUT: a .cube file (path relative to the project folder) or a
    /// built-in camera log conversion ("builtin:apple-log", …); empty = none.
    std::string lut;
    double lutAmount = 1.0;  ///< 0…1 mix with the ungraded picture
    /// How to read the source's colors: "auto" (from the file's tags), "rec709",
    /// "srgb", "display-p3", "rec2020", "rec2020-hlg", "rec2020-pq".
    std::string inputColorSpace = "auto";
    /// Primaries and curves without keyframes: the settings of a look or of a node.
    struct Grade {
        double exposure = 0;
        double brightness = 0;
        double contrast = 0;
        double pivot = 0.5;
        double shadows = 0;
        double highlights = 0;
        double saturation = 0;
        double colorBoost = 0;
        double hue = 0;
        double temperature = 0;
        double tint = 0;
        Wheel lift;
        Wheel gammaWheel;
        Wheel gain;
        Wheel offset;
        std::array<std::vector<Vec2>, 4> curves;  ///< Y, R, G, B
        std::array<std::vector<Vec2>, 6> hslCurves;  ///< see ColorAdjustments::hslCurves
        friend bool operator==(const Grade&, const Grade&) = default;
    };
    /// A creative look on top of the correction above (Lumetri's "Creative"
    /// section, a second node in Resolve): its own primaries and curves, mixed
    /// in by `amount`. The settings are stored in the project, so a project
    /// shows its looks on any machine. Empty `id` = no look.
    struct Look : Grade {
        std::string id;       ///< preset id ("oppenheimer", "custom-…")
        std::string name;     ///< shown in the UI
        double amount = 1.0;  ///< 0…1
        [[nodiscard]] bool isNone() const noexcept { return id.empty(); }
        friend bool operator==(const Look&, const Look&) = default;
    };
    Look look;

    /// A power window: the part of the picture a node changes, in source
    /// coordinates (it follows the clip's crop, zoom and placement).
    struct Window {
        std::string shape;      ///< "" (none), "circle", "rectangle", "gradient"
        double x = 0.5;         ///< center, 0…1 of the source width
        double y = 0.5;         ///< center, 0…1 of the source height
        double width = 0.5;     ///< 0…2 of the source width (circle: horizontal diameter)
        double height = 0.5;    ///< 0…2 of the source height (gradient: the fade's length)
        double rotation = 0;    ///< degrees, clockwise
        double softness = 0.2;  ///< 0…1 edge feather
        bool invert = false;    ///< outside the shape instead of inside
        [[nodiscard]] bool isNone() const noexcept { return shape.empty(); }
        friend bool operator==(const Window&, const Window&) = default;
    };
    /// An HSL key (Resolve's Qualifier): the colors a node changes.
    struct Qualifier {
        bool enabled = false;
        double hue = 0;          ///< center, 0…1 around the color wheel (0 red, ⅓ green, ⅔ blue)
        double hueWidth = 0.08;  ///< half width, 0…0.5 (0.5 = every hue)
        double hueSoft = 0.04;
        double satLow = 0.15;    ///< saturation range (0 gray … 1 pure color)
        double satHigh = 1.0;
        double satSoft = 0.05;
        double lumLow = 0.0;     ///< brightness range (luma, 0…1)
        double lumHigh = 1.0;
        double lumSoft = 0.05;
        bool invert = false;
        friend bool operator==(const Qualifier&, const Qualifier&) = default;
    };
    /// A serial node after the clip's correction and look (Resolve's nodes
    /// 02, 03 …): its own grade, limited to a window, a color key and/or the
    /// person or the background. All three limits multiply.
    struct Node {
        std::string id;       ///< unique within the clip
        std::string label;
        bool enabled = true;
        Grade grade;
        Window window;
        Qualifier qualifier;
        std::string subject;  ///< "" (no limit), "person", "background" (needs a segmenter)
        bool invert = false;  ///< change everything except the selection
        friend bool operator==(const Node&, const Node&) = default;
    };
    static constexpr std::size_t kMaxNodes = 8;
    std::vector<Node> nodes;  ///< in order, at most kMaxNodes
    friend bool operator==(const ColorAdjustments&, const ColorAdjustments&) = default;
};

struct EffectInstance {
    EffectId id;
    std::string type;  ///< e.g. "lectern.blur.gaussian"
    int version = 1;
    bool enabled = true;
    std::map<std::string, Animated<double>> params;
    friend bool operator==(const EffectInstance&, const EffectInstance&) = default;
};

struct TextStyle {
    std::string font = "Inter";
    double size = 64.0;
    int weight = 600;
    std::string color = "#FFFFFFFF";
    std::string alignment = "center";
    double lineSpacing = 1.2;
    double letterSpacing = 0.0;
    std::string background;  ///< empty = none
    double borderWidth = 0.0;
    std::string borderColor;
    double shadowBlur = 0.0;
    std::string shadowColor;
    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

/// How a text clip enters and leaves (docs/RENDERING_PIPELINE.md §4.4).
/// Kinds: "none", "fade", "slide-up", "slide-down", "slide-left",
/// "slide-right", "pop", "zoom", "typewriter", "wipe", "blur".
struct TextAnimation {
    std::string in = "fade";
    std::string out = "fade";
    Time inDuration = Time::fromMilliseconds(150);
    Time outDuration = Time::fromMilliseconds(150);
    friend bool operator==(const TextAnimation&, const TextAnimation&) = default;
};

struct TextContent {
    std::string text;
    std::string preset;  ///< "title", "heading", "caption", ...
    TextStyle style;
    TextAnimation animation;
    friend bool operator==(const TextContent&, const TextContent&) = default;
};

struct SubtitleContent {
    std::string text;
    std::string styleId;
    friend bool operator==(const SubtitleContent&, const SubtitleContent&) = default;
};

enum class ClipKind { Media, Text, Subtitle, Color };

struct Clip {
    ClipId id;
    ClipKind kind = ClipKind::Media;
    std::string name;
    bool enabled = true;
    std::optional<LinkGroupId> linkGroup;
    TimeRange range;           ///< placement on the timeline
    MediaId media;             ///< Media clips only
    Time sourceIn;             ///< media time at range.start
    Rational speed{1, 1};
    Transform transform;
    Animated<Vec4> crop{Vec4{}};
    Animated<double> opacity{1.0};
    AudioProperties audio;
    ColorAdjustments color;
    std::vector<EffectInstance> effects;
    std::optional<TextContent> text;
    std::optional<SubtitleContent> subtitle;
    std::string fillColor;     ///< Color clips

    /// Media time shown at timeline time t (t within range).
    [[nodiscard]] Time sourceTimeAt(Time t) const { return sourceIn + (t - range.start).scaled(speed); }
    /// Media duration consumed by the clip.
    [[nodiscard]] Time sourceDuration() const { return range.duration.scaled(speed); }

    friend bool operator==(const Clip&, const Clip&) = default;
};

enum class TrackKind { Video, Audio, Overlay, Subtitle };

struct Track {
    TrackId id;
    TrackKind kind = TrackKind::Video;
    std::string name;
    bool locked = false;
    bool hidden = false;  ///< visual tracks
    bool muted = false;   ///< audio tracks
    bool solo = false;    ///< audio tracks
    double gainDb = 0.0;  ///< track volume (audio of any track)
    std::vector<Clip> clips;  ///< sorted by range.start, non-overlapping

    /// Clip containing t, or null. O(log n).
    [[nodiscard]] const Clip* clipAt(Time t) const;
    /// Inserts keeping order; fails if the clip would overlap another.
    Status insertClip(Clip clip);
    [[nodiscard]] Time end() const { return clips.empty() ? Time::zero() : clips.back().range.end(); }

    friend bool operator==(const Track&, const Track&) = default;
};

enum class MarkerKind { User, Pause, Chapter };

struct Marker {
    MarkerId id;
    Time time;
    std::string label;
    std::string color = "#F5A524";
    MarkerKind kind = MarkerKind::User;
    friend bool operator==(const Marker&, const Marker&) = default;
};

/// A timeline range with a screen/camera layout preset (TIMELINE_ENGINE.md §5).
struct LayoutRegion {
    LayoutRegionId id;
    TimeRange range;
    std::string preset;  ///< "screen.only", "camera.only", "pip.bottom-right.circle", ...
    friend bool operator==(const LayoutRegion&, const LayoutRegion&) = default;
};

struct ActiveClip {
    const Track* track = nullptr;
    const Clip* clip = nullptr;
    Time sourceTime;
};

class Timeline {
public:
    std::vector<Track> tracks;
    std::vector<Marker> markers;
    std::vector<LayoutRegion> layout;

    [[nodiscard]] Time duration() const;
    [[nodiscard]] const Clip* findClip(const ClipId& id) const;
    [[nodiscard]] Track* findTrack(const TrackId& id);
    [[nodiscard]] const Track* findTrack(const TrackId& id) const;
    /// Enabled clips under t on visible tracks, in track order (bottom → top).
    [[nodiscard]] std::vector<ActiveClip> activeClipsAt(Time t) const;
    /// Checks the invariants in docs/TIMELINE_ENGINE.md §2.
    [[nodiscard]] Status validate(const std::function<bool(const MediaId&)>& mediaExists = {}) const;

    friend bool operator==(const Timeline&, const Timeline&) = default;
};

[[nodiscard]] std::string_view toString(TrackKind kind) noexcept;
[[nodiscard]] std::string_view toString(ClipKind kind) noexcept;
[[nodiscard]] std::string_view toString(Interpolation interp) noexcept;
[[nodiscard]] std::string_view toString(FitMode mode) noexcept;
[[nodiscard]] std::string_view toString(MarkerKind kind) noexcept;

}  // namespace lectern::timeline
