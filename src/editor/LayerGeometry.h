#pragma once

// Where each layer of a render plan is visible on the canvas — the same
// geometry the compositor draws — for picking and manipulating layers on the
// preview (docs/RENDERING_PIPELINE.md §4.5).

#include "editor/RenderPlan.h"

#include <vector>

namespace lectern::editor {

struct LayerBounds {
    NormRect rect;        ///< canvas fractions
    bool circle = false;  ///< hit-test as an ellipse
};

/// The rectangle a media layer's picture occupies: the box itself when it
/// fills (camera), the fitted picture inside the box otherwise (screen).
[[nodiscard]] NormRect visibleMediaRect(const VisualLayer& layer, int canvasWidth, int canvasHeight);

/// Visible bounds of every layer of `plan`, in plan order: fitted media,
/// text blocks as animated at this instant, subtitle boxes (moved above titles).
[[nodiscard]] std::vector<LayerBounds> layerBounds(const RenderPlan& plan);

/// Index of the topmost visible layer containing the canvas point (fractions), or −1.
[[nodiscard]] int layerAt(const RenderPlan& plan, double x, double y);

}  // namespace lectern::editor
