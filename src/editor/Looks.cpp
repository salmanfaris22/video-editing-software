#include "editor/Looks.h"

#include "editor/ColorGrading.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace lectern::editor {

namespace {

using Wheel = timeline::ColorAdjustments::Wheel;
using Curve = std::vector<timeline::Vec2>;

Wheel wheel(double x, double y, double master = 0) { return Wheel{x, y, master}; }

Curve curve(std::initializer_list<std::pair<double, double>> points) {
    Curve out;
    for (const auto& [x, y] : points) out.push_back({x, y});
    return out;
}

template <typename Setup>
LookPreset look(const char* id, const char* name, const char* category, const char* description, Setup setup) {
    LookPreset p;
    p.category = category;
    p.description = description;
    p.settings.id = id;
    p.settings.name = name;
    setup(p.settings);
    return p;
}

// Values were tuned with a model of colorCurves + saturation + Color Boost on
// reference patches: gray ramp, light and dark skin, sky, foliage, saturated
// red. Every look keeps skin believable and avoids crushing a 15 % gray to
// black (except the deliberate Noir/B&W contrast).
std::vector<LookPreset> makeLooks() {
    std::vector<LookPreset> v;
    v.push_back(look("oppenheimer", "Oppenheimer", "Film",
                     "Warm large-format film: rich skin, cool deep shadows, soft highlight roll-off.", [](LookSettings& l) {
                         l.contrast = 0.12;
                         l.pivot = 0.42;
                         l.shadows = -0.04;
                         l.highlights = -0.06;
                         l.lift = wheel(0.08, -0.16, -0.01);
                         l.gammaWheel = wheel(-0.03, 0.05);
                         l.gain = wheel(-0.07, 0.10);
                         l.colorBoost = 0.06;
                         l.curves[0] = curve({{0, 0.025}, {0.25, 0.215}, {0.75, 0.8}, {1, 0.97}});
                     }));
    v.push_back(look("teal-orange", "Teal & Orange", "Cinematic",
                     "Blockbuster contrast: teal shadows, warm skin and highlights.", [](LookSettings& l) {
                         l.lift = wheel(0.22, -0.28);
                         l.gain = wheel(-0.2, 0.2);
                         l.colorBoost = 0.15;
                         l.contrast = 0.12;
                         l.saturation = 0.05;
                         l.highlights = -0.06;
                     }));
    v.push_back(look("dark-green", "Dark Green", "Cinematic",
                     "Moody green cast through the image, muted color, deep shadows.", [](LookSettings& l) {
                         l.tint = -0.35;
                         l.temperature = -0.08;
                         l.exposure = -0.1;
                         l.lift = wheel(-0.15, -0.35);
                         l.gain = wheel(-0.18, -0.1);
                         l.saturation = -0.28;
                         l.contrast = 0.18;
                         l.shadows = -0.08;
                         l.curves[1] = curve({{0, 0}, {0.5, 0.46}, {1, 1}});
                         l.curves[2] = curve({{0, 0}, {0.5, 0.53}, {1, 1}});
                     }));
    v.push_back(look("warm-film", "Warm Film", "Film", "Warm print-film color with soft blacks and gentle highlights.",
                     [](LookSettings& l) {
                         l.temperature = 0.18;
                         l.tint = 0.04;
                         l.gain = wheel(-0.08, 0.1);
                         l.saturation = 0.04;
                         l.colorBoost = 0.08;
                         l.contrast = 0.1;
                         l.curves[0] = curve({{0, 0.045}, {0.5, 0.515}, {1, 0.955}});
                     }));
    v.push_back(look("bleach-bypass", "Bleach Bypass", "Film", "Silvery, low-saturation, high-contrast war-film look.",
                     [](LookSettings& l) {
                         l.saturation = -0.5;
                         l.contrast = 0.2;
                         l.highlights = 0.08;
                         l.shadows = -0.03;
                         l.temperature = -0.04;
                         l.curves[0] = curve({{0, 0.015}, {0.3, 0.28}, {0.7, 0.75}, {1, 1}});
                     }));
    v.push_back(look("golden-hour", "Golden Hour", "Cinematic", "Late-sun warmth with glowing skin and soft highlights.",
                     [](LookSettings& l) {
                         l.temperature = 0.28;
                         l.tint = 0.06;
                         l.exposure = 0.05;
                         l.gain = wheel(-0.12, 0.18);
                         l.colorBoost = 0.15;
                         l.highlights = -0.12;
                     }));
    v.push_back(look("cool-thriller", "Cool Thriller", "Cinematic", "Cold, desaturated tension with green-teal shadows.",
                     [](LookSettings& l) {
                         l.temperature = -0.1;
                         l.tint = -0.15;
                         l.saturation = -0.35;
                         l.contrast = 0.2;
                         l.gain = wheel(-0.15, 0.05);
                         l.lift = wheel(0.1, -0.2);
                     }));
    v.push_back(look("dune-desert", "Dune Desert", "Cinematic", "Sun-baked sand tones, lifted warm blacks, quiet color.",
                     [](LookSettings& l) {
                         l.temperature = 0.32;
                         l.tint = 0.08;
                         l.saturation = -0.15;
                         l.contrast = -0.04;
                         l.lift = wheel(-0.08, 0.12);
                         l.curves[0] = curve({{0, 0.035}, {1, 0.99}});
                     }));
    v.push_back(look("neon-night", "Neon Night", "Mood", "Electric blue shadows and magenta-warm highlights for night city shots.",
                     [](LookSettings& l) {
                         l.lift = wheel(0.35, -0.2);
                         l.gain = wheel(-0.12, 0.3);
                         l.colorBoost = 0.22;
                         l.contrast = 0.2;
                         l.exposure = -0.15;
                         l.highlights = -0.08;
                     }));
    v.push_back(look("moonlight-blue", "Moonlight Blue", "Mood", "Day-for-night: darker, cool and muted.", [](LookSettings& l) {
        l.temperature = -0.42;
        l.exposure = -0.35;
        l.saturation = -0.3;
        l.lift = wheel(0.25, -0.08);
        l.contrast = 0.1;
    }));
    v.push_back(look("vintage-fade", "Vintage Fade", "Film", "Faded blacks, softened whites and warm, washed color.",
                     [](LookSettings& l) {
                         l.curves[0] = curve({{0, 0.09}, {1, 0.91}});
                         l.saturation = -0.25;
                         l.temperature = 0.13;
                         l.offset = wheel(-0.04, 0.04);
                         l.contrast = -0.08;
                     }));
    v.push_back(look("pastel-dream", "Pastel Dream", "Mood", "Soft, bright and low-contrast with rosy pastel color.",
                     [](LookSettings& l) {
                         l.contrast = -0.15;
                         l.exposure = 0.12;
                         l.saturation = -0.2;
                         l.temperature = 0.06;
                         l.tint = 0.12;
                         l.lift = wheel(0.05, 0.08);
                         l.gain = wheel(0.04, 0.08);
                         l.curves[0] = curve({{0, 0.07}, {1, 0.97}});
                     }));
    v.push_back(look("sepia-memory", "Sepia Memory", "Film", "Old photograph: brown-toned, nearly monochrome, faded.",
                     [](LookSettings& l) {
                         l.saturation = -0.7;
                         l.temperature = 1.0;
                         l.tint = 0.12;
                         l.gain = wheel(-0.25, 0.2);
                         l.lift = wheel(-0.15, 0.12);
                         l.contrast = 0.05;
                         l.curves[0] = curve({{0, 0.06}, {1, 0.94}});
                     }));
    v.push_back(look("oppenheimer-bw", "Oppenheimer B&W", "Black & White",
                     "Deep, silky black-and-white with rich midtones.", [](LookSettings& l) {
                         l.saturation = -1.0;
                         l.contrast = 0.22;
                         l.pivot = 0.45;
                         l.shadows = -0.04;
                         l.curves[0] = curve({{0, 0.01}, {0.3, 0.25}, {0.7, 0.78}, {1, 0.99}});
                     }));
    v.push_back(look("noir", "Noir", "Black & White", "Hard, graphic black-and-white.", [](LookSettings& l) {
        l.saturation = -1.0;
        l.contrast = 0.25;
        l.shadows = -0.04;
        l.highlights = 0.05;
        l.curves[0] = curve({{0, 0.01}, {0.3, 0.26}, {0.7, 0.78}, {1, 1}});
    }));
    v.push_back(look("clean-creator", "Clean Creator", "Clean", "Bright, crisp and natural for talking heads and screen demos.",
                     [](LookSettings& l) {
                         l.exposure = 0.08;
                         l.contrast = 0.1;
                         l.colorBoost = 0.18;
                         l.highlights = -0.1;
                         l.shadows = 0.06;
                     }));
    return v;
}

double clampUnit(double v) { return std::clamp(v, -1.0, 1.0); }

Wheel scaled(const Wheel& w, double a) { return Wheel{w.x * a, w.y * a, w.master * a}; }

Wheel added(Wheel a, const Wheel& b) {
    a.x += b.x;
    a.y += b.y;
    if (const double r = std::hypot(a.x, a.y); r > 1.0) {
        a.x /= r;
        a.y /= r;
    }
    a.master = clampUnit(a.master + b.master);
    return a;
}

bool isDiagonal(const Curve& c) {
    return c.size() < 2 || std::all_of(c.begin(), c.end(), [](const timeline::Vec2& p) { return std::abs(p.x - p.y) < 1e-6; });
}

/// `then` applied after `first`, resampled so the renderers see one curve.
Curve compose(const Curve& first, const Curve& then) {
    if (isDiagonal(then)) return first;
    if (first.empty()) return then;
    constexpr int kSamples = 33;
    Curve out;
    out.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        const double x = static_cast<double>(i) / (kSamples - 1);
        const double y = evaluateCurve(then, std::clamp(evaluateCurve(first, x), 0.0, 1.0));
        out.push_back({x, std::clamp(y, 0.0, 1.0)});
    }
    return out;
}

}  // namespace

const std::vector<LookPreset>& builtinLooks() {
    static const std::vector<LookPreset> looks = makeLooks();
    return looks;
}

const LookPreset* findBuiltinLook(std::string_view id) {
    for (const auto& l : builtinLooks()) {
        if (l.settings.id == id) return &l;
    }
    return nullptr;
}

LookSettings scaleLook(const LookSettings& look, double amount) {
    const double a = std::clamp(amount, 0.0, 1.0);
    LookSettings s = look;
    s.amount = 1.0;
    s.exposure *= a;
    s.brightness *= a;
    s.contrast *= a;
    s.shadows *= a;
    s.highlights *= a;
    s.saturation *= a;
    s.colorBoost *= a;
    s.hue *= a;
    s.temperature *= a;
    s.tint *= a;
    s.lift = scaled(look.lift, a);
    s.gammaWheel = scaled(look.gammaWheel, a);
    s.gain = scaled(look.gain, a);
    s.offset = scaled(look.offset, a);
    for (auto& c : s.curves) {
        for (auto& p : c) p.y = p.x + (p.y - p.x) * a;
    }
    return s;
}

ColorParams applyLook(const ColorParams& base, const LookSettings& look) {
    if (look.isNone() || look.amount <= 0.0) return base;
    return combineGrades(base, scaleLook(look, look.amount));
}

ColorParams paramsOf(const timeline::ColorAdjustments::Grade& grade) { return combineGrades(ColorParams{}, grade); }

ColorParams combineGrades(const ColorParams& base, const timeline::ColorAdjustments::Grade& l) {
    ColorParams out = base;
    out.exposure = std::clamp(base.exposure + l.exposure, -2.0, 2.0);
    out.brightness = clampUnit(base.brightness + l.brightness);
    if (l.contrast != 0.0) {
        if (base.contrast == 0.0) {
            out.contrast = l.contrast;
            out.pivot = l.pivot;
        } else {
            // (x − pb)·cb + pb, then (· − pl)·cl + pl  ==  (x − p)·cb·cl + p.
            const double cb = 1.0 + base.contrast;
            const double cl = 1.0 + l.contrast;
            const double slope = cb * cl;
            if (std::abs(1.0 - slope) > 1e-6) {
                out.pivot = std::clamp(((base.pivot - l.pivot) * cl + l.pivot - base.pivot * slope) / (1.0 - slope), 0.0, 1.0);
            }
            out.contrast = clampUnit(slope - 1.0);
        }
    }
    out.shadows = clampUnit(base.shadows + l.shadows);
    out.highlights = clampUnit(base.highlights + l.highlights);
    out.saturation = clampUnit(base.saturation + l.saturation + base.saturation * l.saturation);  // (1+a)(1+b) − 1
    out.colorBoost = clampUnit(base.colorBoost + l.colorBoost);
    double hue = base.hue + l.hue;  // a full turn is ±1
    if (hue > 1.0) hue -= 2.0;
    if (hue < -1.0) hue += 2.0;
    out.hue = hue;
    out.temperature = clampUnit(base.temperature + l.temperature);
    out.tint = clampUnit(base.tint + l.tint);
    out.lift = added(base.lift, l.lift);
    out.gamma = added(base.gamma, l.gammaWheel);
    out.gain = added(base.gain, l.gain);
    out.offset = added(base.offset, l.offset);
    for (std::size_t i = 0; i < 4; ++i) out.curves[i] = compose(base.curves[i], l.curves[i]);
    return out;
}

LookSettings lookFromGrade(const ColorParams& g) {
    LookSettings l;
    l.exposure = g.exposure;
    l.brightness = g.brightness;
    l.contrast = g.contrast;
    l.pivot = g.pivot;
    l.shadows = g.shadows;
    l.highlights = g.highlights;
    l.saturation = g.saturation;
    l.colorBoost = g.colorBoost;
    l.hue = g.hue;
    l.temperature = g.temperature;
    l.tint = g.tint;
    l.lift = g.lift;
    l.gammaWheel = g.gamma;
    l.gain = g.gain;
    l.offset = g.offset;
    l.curves = g.curves;
    return l;
}

}  // namespace lectern::editor
