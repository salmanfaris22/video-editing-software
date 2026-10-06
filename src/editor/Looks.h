#pragma once

// Cinematic looks (docs/v2/COLOR_GRADING_COMPARISON.md §2.11): creative
// grades that sit on top of a clip's own correction and are mixed in by an
// amount, like Lumetri's Creative looks or a second node in Resolve. The
// built-in looks were tuned on reference colors (skin, sky, foliage, a gray
// ramp) with a model of the grading math, so they stay natural at 100 %.

#include "editor/RenderPlan.h"
#include "timeline/Timeline.h"

#include <string>
#include <string_view>
#include <vector>

namespace lectern::editor {

using LookSettings = timeline::ColorAdjustments::Look;

struct LookPreset {
    std::string category;  ///< "Film", "Cinematic", "Mood", "Black & White", "Clean"
    std::string description;
    LookSettings settings;  ///< id and name filled in, amount 1
};

/// The built-in looks in display order.
[[nodiscard]] const std::vector<LookPreset>& builtinLooks();
[[nodiscard]] const LookPreset* findBuiltinLook(std::string_view id);

/// `look` moved toward neutral: 0 = no change … 1 = the full look. Curves
/// move toward the diagonal; the contrast pivot stays where the look put it.
[[nodiscard]] LookSettings scaleLook(const LookSettings& look, double amount);

/// The grade `base` with `look` on top at look.amount, as one set of
/// parameters for the renderers. Offsets add (exposure in stops, wheels,
/// temperature …), gains multiply (contrast about the combined pivot,
/// saturation) and curves compose (the look's curve after the base's). With
/// a neutral base the result is exactly the look; at amount 0 exactly `base`.
[[nodiscard]] ColorParams applyLook(const ColorParams& base, const LookSettings& look);

/// A grade's settings as a look, to save a clip's grade for reuse. The LUT
/// and the input color space are not part of a look.
[[nodiscard]] LookSettings lookFromGrade(const ColorParams& grade);

}  // namespace lectern::editor
