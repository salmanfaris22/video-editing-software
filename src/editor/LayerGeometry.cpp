#include "editor/LayerGeometry.h"

#include "editor/Compositor.h"

#include <QImage>

#include <algorithm>

namespace lectern::editor {

namespace {

/// Font metrics need a paint device; the compositor paints on QImages.
QPaintDevice* metricsDevice() {
    thread_local QImage device(1, 1, QImage::Format_ARGB32_Premultiplied);
    return &device;
}

NormRect fraction(const QRectF& r, double W, double H) { return {r.x() / W, r.y() / H, r.width() / W, r.height() / H}; }

}  // namespace

NormRect visibleMediaRect(const VisualLayer& l, int canvasWidth, int canvasHeight) {
    if (l.fill || l.sourceAspect <= 0) return l.box;
    const double W = std::max(1, canvasWidth);
    const double H = std::max(1, canvasHeight);
    const double boxW = l.box.w * W;
    const double boxH = l.box.h * H;
    if (boxW <= 0 || boxH <= 0) return l.box;
    const double scale = std::min(boxW / l.sourceAspect, boxH);  // picture height in pixels
    const double w = scale * l.sourceAspect / W;
    const double h = scale / H;
    return {l.box.x + (l.box.w - w) / 2, l.box.y + (l.box.h - h) / 2, w, h};
}

std::vector<LayerBounds> layerBounds(const RenderPlan& plan) {
    const double W = plan.width;
    const double H = plan.height;
    std::vector<LayerBounds> out;
    out.reserve(plan.layers.size());
    std::vector<QRectF> titles;
    for (const VisualLayer& l : plan.layers) {
        LayerBounds b;
        switch (l.kind) {
            case LayerKind::Media:
                b.rect = visibleMediaRect(l, plan.width, plan.height);
                b.circle = l.circle;
                break;
            case LayerKind::Text: {
                const TextBlock t = layoutText(l, W, H, metricsDevice());
                const QRectF shown = animatedTextRect(l, t, W, H);
                if (l.opacity > 0.01) titles.push_back(shown);
                b.rect = fraction(shown, W, H);
                break;
            }
            case LayerKind::Subtitle:
                b.rect = fraction(subtitleRect(l, plan.style, W, H, titles, metricsDevice()), W, H);
                break;
        }
        out.push_back(b);
    }
    return out;
}

int layerAt(const RenderPlan& plan, double x, double y) {
    const std::vector<LayerBounds> bounds = layerBounds(plan);
    for (int i = static_cast<int>(bounds.size()) - 1; i >= 0; --i) {
        const VisualLayer& l = plan.layers[static_cast<std::size_t>(i)];
        if (l.opacity <= 0.02) continue;
        const NormRect& r = bounds[static_cast<std::size_t>(i)].rect;
        if (bounds[static_cast<std::size_t>(i)].circle) {
            const double dx = (x - (r.x + r.w / 2)) / std::max(1e-9, r.w / 2);
            const double dy = (y - (r.y + r.h / 2)) / std::max(1e-9, r.h / 2);
            if (dx * dx + dy * dy <= 1.0) return i;
        } else if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
            return i;
        }
    }
    return -1;
}

}  // namespace lectern::editor
