#pragma once

// Screen/camera layout presets (docs/TIMELINE_ENGINE.md §5). One source of
// truth for the compositor (preview and export) and the UI's preset tiles.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lectern::editor {

/// A rectangle in canvas fractions (x, w of the width; y, h of the height).
struct NormRect {
    double x = 0;
    double y = 0;
    double w = 1;
    double h = 1;
    friend constexpr bool operator==(const NormRect&, const NormRect&) = default;
};

struct LayoutSlots {
    std::optional<NormRect> screen;
    std::optional<NormRect> camera;
    bool circleCamera = false;
    bool fullBleedCamera = false;  ///< camera fills the canvas (no shape/border)
};

struct LayoutPresetInfo {
    std::string id;
    std::string name;
};

/// Presets in UI order.
[[nodiscard]] const std::vector<LayoutPresetInfo>& layoutPresets();
[[nodiscard]] bool isKnownLayout(std::string_view id);

/// Slots for `preset` on a canvas of `width`×`height` pixels. Aspect-aware:
/// side-by-side layouts stack vertically on portrait canvases. Unknown
/// presets behave like "screen.only".
[[nodiscard]] LayoutSlots layoutSlots(std::string_view preset, int width, int height);

}  // namespace lectern::editor
