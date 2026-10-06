#include "editor/LayoutPresets.h"

#include <algorithm>

namespace lectern::editor {

const std::vector<LayoutPresetInfo>& layoutPresets() {
    static const std::vector<LayoutPresetInfo> presets = {
        {"screen.only", "Screen"},
        {"pip.bottom-right.rounded", "Camera corner"},
        {"pip.bottom-right.circle", "Circle camera"},
        {"pip.bottom-left.rounded", "Camera left"},
        {"split.screen-left", "Side by side"},
        {"split.camera-left", "Camera first"},
        {"camera.only", "Camera"},
    };
    return presets;
}

bool isKnownLayout(std::string_view id) {
    const auto& all = layoutPresets();
    return std::any_of(all.begin(), all.end(), [id](const LayoutPresetInfo& p) { return p.id == id; });
}

LayoutSlots layoutSlots(std::string_view preset, int width, int height) {
    const double W = std::max(1, width);
    const double H = std::max(1, height);
    const bool portrait = H > W * 1.05;
    const double unit = std::min(W, H);
    const double m = unit * 0.035;  // margin
    auto norm = [&](double x, double y, double w, double h) { return NormRect{x / W, y / H, w / W, h / H}; };
    LayoutSlots s;

    if (preset == "camera.only") {
        s.camera = NormRect{};
        s.fullBleedCamera = true;
        return s;
    }
    if (preset.starts_with("pip.")) {
        s.screen = NormRect{};
        const bool left = preset.find("left") != std::string_view::npos;
        const bool top = preset.find("top") != std::string_view::npos;
        double cw = 0;
        double ch = 0;
        if (preset.ends_with("circle")) {
            s.circleCamera = true;
            cw = ch = unit * (portrait ? 0.40 : 0.32);
        } else if (portrait) {
            cw = W * 0.42;
            ch = cw;  // square bubble reads better on tall canvases
        } else {
            ch = H * 0.30;
            cw = ch * 16.0 / 9.0;
        }
        const double x = left ? m : W - m - cw;
        const double y = top ? m : H - m - ch;
        s.camera = norm(x, y, cw, ch);
        return s;
    }
    if (preset.starts_with("split.")) {
        const bool cameraFirst = preset == "split.camera-left";
        if (portrait) {  // stacked: screen above camera (or the reverse)
            const double split = H * 0.56;
            const NormRect top = norm(m, m, W - 2 * m, split - 1.5 * m);
            const NormRect bottom = norm(m, split + 0.5 * m, W - 2 * m, H - split - 1.5 * m);
            s.screen = cameraFirst ? bottom : top;
            s.camera = cameraFirst ? top : bottom;
        } else {
            const double split = W * 0.66;
            const NormRect wide = norm(m, m, split - 1.5 * m, H - 2 * m);
            const NormRect narrow = norm(split + 0.5 * m, m, W - split - 1.5 * m, H - 2 * m);
            s.screen = cameraFirst ? norm(W - split + 0.5 * m, m, split - 1.5 * m, H - 2 * m) : wide;
            s.camera = cameraFirst ? norm(m, m, W - split - 1.5 * m, H - 2 * m) : narrow;
        }
        return s;
    }
    s.screen = NormRect{};  // "screen.only" and unknown presets
    return s;
}

}  // namespace lectern::editor
