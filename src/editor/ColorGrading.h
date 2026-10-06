#pragma once

// Color grading (docs/RENDERING_PIPELINE.md §4.3): 3D LUTs (.cube files and
// built-in camera log conversions), primary color wheels and the basic
// adjustments, applied to 8-bit images by the compositor.

#include "core/Error.h"
#include "editor/RenderPlan.h"

#include <QImage>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace lectern::editor {

/// A 3D lookup table on [domainMin, domainMax]³, red index fastest (.cube order).
struct Lut3D {
    int size = 0;
    std::array<float, 3> domainMin{0, 0, 0};
    std::array<float, 3> domainMax{1, 1, 1};
    std::vector<float> table;  ///< size³ × RGB
    std::string title;

    /// Trilinear lookup of an RGB triple (clamped to the domain).
    [[nodiscard]] std::array<float, 3> sample(float r, float g, float b) const noexcept;
};

/// Parses a .cube file (Adobe/Resolve format; 3D, or 1D expanded to 3D).
[[nodiscard]] Result<Lut3D> parseCubeLut(std::string_view text);
[[nodiscard]] Result<Lut3D> loadCubeLut(const std::filesystem::path& path);

/// Built-in conversions from camera log footage to Rec.709 (id, name).
struct BuiltinLut {
    std::string id;    ///< "builtin:apple-log", ...
    std::string name;  ///< "Apple Log → Rec.709"
};
[[nodiscard]] const std::vector<BuiltinLut>& builtinLuts();
/// Generated once per id (33³); null for unknown ids.
[[nodiscard]] std::shared_ptr<const Lut3D> builtinLut(std::string_view id);

/// Loads LUT references ("builtin:…" or a path relative to a project folder)
/// and keeps them; thread-safe.
class LutCache {
public:
    [[nodiscard]] std::shared_ptr<const Lut3D> get(const std::string& ref, const std::filesystem::path& projectDir);

private:
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<const Lut3D>> luts_;
};

// ---- Colorimetry helpers (exposed for tests) ----------------------------------

using Matrix3 = std::array<std::array<double, 3>, 3>;
struct Chromaticity {
    double x;
    double y;
};
/// Linear RGB → XYZ for the given primaries and white point.
[[nodiscard]] Matrix3 rgbToXyz(Chromaticity red, Chromaticity green, Chromaticity blue, Chromaticity white);
[[nodiscard]] Matrix3 inverse(const Matrix3& m);
[[nodiscard]] Matrix3 multiply(const Matrix3& a, const Matrix3& b);
/// Converts linear RGB in a camera gamut ("rec2020", "s-gamut3.cine", "v-gamut", "awg3") to linear Rec.709.
[[nodiscard]] Matrix3 gamutToRec709(std::string_view gamut);
/// Log encoding → scene-linear reflectance (0.18 = middle gray).
[[nodiscard]] double decodeLog(std::string_view curve, double code);

/// RGB offsets of a color wheel's puck (x = Cb, y = Cr direction), zero luma.
[[nodiscard]] std::array<double, 3> wheelChroma(const ColorParams::Wheel& wheel);

// ---- Input color (Phase 1.2) -------------------------------------------------
// The GPU shader (src/render/shaders/layer.frag) mirrors these functions.

inline constexpr double kHdrReferenceWhite = 203.0;  ///< nits mapped to SDR white (ITU-R BT.2408)
inline constexpr double kToneKnee = 0.8;             ///< SDR level where highlight roll-off starts

/// Linear-light conversion from the source gamut to Rec.709 (row-major).
[[nodiscard]] Matrix3 gamutToBt709(Primaries primaries);
/// One source pixel (0…1 code values) → Rec.709 display-referred code values (0…1).
[[nodiscard]] std::array<double, 3> convertInputColor(const InputColor& input, double r, double g, double b);
/// Converts `image` in place (RGB32 / ARGB32 premultiplied / BGR30 → RGB32 or ARGB32 premultiplied).
void applyInputColor(QImage& image, const InputColor& input);

/// The per-channel 8-bit curves of a grade (everything except the LUT and
/// saturation): out = curve[channel][in]. Shared by the CPU and GPU renderers.
using ColorCurves = std::array<std::array<std::uint8_t, 256>, 3>;
[[nodiscard]] ColorCurves colorCurves(const ColorParams& color);

/// Color Boost (vibrance) and Hue rotation on one pixel (0…1), after saturation.
/// The GPU shader mirrors it.
[[nodiscard]] std::array<double, 3> boostAndHue(const ColorParams& color, double r, double g, double b);

/// Grades `image` in place: LUT (mixed by lutAmount), then exposure,
/// brightness, contrast, temperature/tint, lift/gamma/gain, saturation.
void applyColor(QImage& image, const ColorParams& color, const Lut3D* lut = nullptr);

}  // namespace lectern::editor
