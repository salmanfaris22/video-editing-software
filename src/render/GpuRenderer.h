#pragma once

// GPU renderer on Qt RHI (Metal on macOS, Direct3D 11 on Windows). Draws the
// same RenderPlan as the CPU compositor (editor/Compositor.cpp) — media
// layers with crop/zoom/fit, rounded and circle shapes, borders, shadows,
// vignette, color curves, LUTs, blur and background blur on the GPU; text
// and subtitles rasterized with the same QPainter code and composited in
// layer order — offscreen, then read back into the target image.
// Tested against the CPU compositor in tests/render.

#include "editor/FrameRenderer.h"

#include <memory>
#include <string>

namespace lectern::render {

/// A GPU renderer for the calling thread, or null when no GPU backend is
/// available here. The GPU device is created on first use, on the thread
/// that renders; if it fails then, the renderer falls back to the CPU.
[[nodiscard]] std::unique_ptr<editor::FrameRenderer> makeGpuRenderer();

/// Probes the GPU backend once and, when it works, registers the GPU
/// renderer as the process default (editor::setRendererFactory). Returns the
/// backend name ("Metal", "Direct3D 11"), or an empty string (CPU stays).
std::string installGpuRenderer();

}  // namespace lectern::render
