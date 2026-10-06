#pragma once

// The renderer that draws a RenderPlan into an image. The CPU Compositor is
// the reference implementation and the default; the GPU renderer
// (src/render) registers itself at startup with setRendererFactory, so the
// preview, the exporter and the CLI pick it up without depending on it.
// LECTERN_RENDERER=cpu forces the CPU renderer.

#include "editor/RenderPlan.h"

#include <QImage>
#include <QSizeF>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

namespace lectern::editor {

class FrameRenderer {
public:
    /// Supplies the image of a media layer for a destination box of the given pixel size.
    using ImageSource = std::function<QImage(const VisualLayer& layer, QSizeF boxPixels)>;

    virtual ~FrameRenderer() = default;
    /// The project folder LUT paths are relative to.
    virtual void setProjectDirectory(std::filesystem::path dir) = 0;
    /// Renders `plan` into `target` (RGB32), scaling the canvas to the target's size.
    virtual void render(const RenderPlan& plan, QImage& target, const ImageSource& images) = 0;
    /// Renders `plan` at `size` straight to BT.709 limited-range NV12 (the
    /// encoder's input): Y plane size.w × size.h, interleaved CbCr plane
    /// size.w/2 × size.h/2. Returns false when this renderer cannot; the
    /// caller then renders RGB and converts (the CPU renderer always does).
    virtual bool renderNv12(const RenderPlan& plan, QSize size, std::uint8_t* y, int yStride, std::uint8_t* uv, int uvStride,
                            const ImageSource& images) {
        (void)plan, (void)size, (void)y, (void)yStride, (void)uv, (void)uvStride, (void)images;
        return false;
    }
    /// "cpu" or "gpu (Metal)", … — for logs and diagnostics.
    [[nodiscard]] virtual std::string name() const = 0;
};

using RendererFactory = std::function<std::unique_ptr<FrameRenderer>()>;
/// Process-wide: the factory makeRenderer uses (empty = CPU).
void setRendererFactory(RendererFactory factory);
/// A new renderer for the calling thread: the registered one, or the CPU compositor.
[[nodiscard]] std::unique_ptr<FrameRenderer> makeRenderer();
/// Always the CPU compositor (reference, tests, fallback).
[[nodiscard]] std::unique_ptr<FrameRenderer> makeCpuRenderer();

}  // namespace lectern::editor
