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

NormRect sourceFrameRect(const VisualLayer& l, int canvasWidth, int canvasHeight) {
    const double W = std::max(1, canvasWidth);
    const double H = std::max(1, canvasHeight);
    const double A = l.sourceAspect > 0 ? l.sourceAspect : 16.0 / 9.0;
    // The source is A × 1 ("source heights"); the same steps as Compositor::drawMedia.
    double sx = 0, sy = 0, sw = A, sh = 1;
    if (l.cropL > 0 || l.cropT > 0 || l.cropR > 0 || l.cropB > 0) {
        sx = A * l.cropL;
        sy = l.cropT;
        sw = A * (1.0 - l.cropL - l.cropR);
        sh = 1.0 - l.cropT - l.cropB;
    }
    if (l.zoom > 1.0001) {
        const double w = sw / l.zoom;
        const double h = sh / l.zoom;
        const double cx = std::clamp(l.zoomX * A, w / 2, A - w / 2);
        const double cy = std::clamp(l.zoomY, h / 2, 1.0 - h / 2);
        sx = cx - w / 2;
        sy = cy - h / 2;
        sw = w;
        sh = h;
    }
    const double bx = l.box.x * W, by = l.box.y * H, bw = l.box.w * W, bh = l.box.h * H;
    if (bw <= 0 || bh <= 0 || sw <= 0 || sh <= 0) return l.box;
    double dx = bx, dy = by, dw = bw, dh = bh;
    if (l.fill) {
        const double boxAspect = bw / bh;
        if (sw / sh > boxAspect) {
            const double w = sh * boxAspect;
            sx += (sw - w) / 2;
            sw = w;
        } else {
            const double h = sw / boxAspect;
            sy += (sh - h) / 2;
            sh = h;
        }
    } else {
        const double scale = std::min(bw / sw, bh / sh);
        dw = sw * scale;
        dh = sh * scale;
        dx = bx + (bw - dw) / 2;
        dy = by + (bh - dh) / 2;
    }
    const double k = dw / sw;  // canvas pixels per source unit
    return {(dx - sx * k) / W, (dy - sy * k) / H, A * k / W, k / H};
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
