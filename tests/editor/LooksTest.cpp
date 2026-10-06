// Cinematic looks: the presets, the look-on-top-of-the-correction math, and
// what each look does to reference colors (skin, sky, foliage, a gray ramp).

#include "editor/ColorGrading.h"
#include "editor/Looks.h"
#include "editor/RenderPlan.h"
#include "timeline/TimelineJson.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <set>
#include <string>

using namespace lectern;
using namespace lectern::editor;

namespace {

struct Patch {
    const char* name;
    std::array<int, 3> rgb;
};

constexpr Patch kPatches[] = {
    {"black", {10, 10, 11}},    {"shadow", {38, 38, 38}},    {"gray", {115, 115, 115}},  {"light", {204, 204, 204}},
    {"white", {247, 247, 247}}, {"skin", {209, 148, 120}},   {"darkSkin", {115, 76, 61}}, {"sky", {115, 166, 230}},
    {"foliage", {64, 115, 46}}, {"red", {204, 38, 31}},
};
constexpr int kCount = static_cast<int>(std::size(kPatches));

/// One pixel per patch, graded by the CPU reference renderer.
std::array<std::array<int, 3>, kCount> grade(const ColorParams& c) {
    QImage img(kCount, 1, QImage::Format_RGB32);
    for (int i = 0; i < kCount; ++i) img.setPixel(i, 0, qRgb(kPatches[i].rgb[0], kPatches[i].rgb[1], kPatches[i].rgb[2]));
    applyColor(img, c);
    std::array<std::array<int, 3>, kCount> out{};
    for (int i = 0; i < kCount; ++i) {
        const QRgb p = img.pixel(i, 0);
        out[i] = {qRed(p), qGreen(p), qBlue(p)};
    }
    return out;
}

int index(const char* name) {
    for (int i = 0; i < kCount; ++i) {
        if (std::string(kPatches[i].name) == name) return i;
    }
    return -1;
}

ColorParams lookOnly(const char* id) {
    const LookPreset* p = findBuiltinLook(id);
    EXPECT_NE(p, nullptr) << id;
    return p ? applyLook(ColorParams{}, p->settings) : ColorParams{};
}

}  // namespace

TEST(Looks, PresetsAreCompleteAndUnique) {
    const std::set<std::string> categories{"Film", "Cinematic", "Mood", "Black & White", "Clean"};
    std::set<std::string> ids;
    ASSERT_GE(builtinLooks().size(), 16u);
    for (const auto& p : builtinLooks()) {
        EXPECT_FALSE(p.settings.id.empty());
        EXPECT_FALSE(p.settings.name.empty());
        EXPECT_FALSE(p.description.empty()) << p.settings.id;
        EXPECT_TRUE(categories.contains(p.category)) << p.category;
        EXPECT_DOUBLE_EQ(p.settings.amount, 1.0);
        EXPECT_TRUE(ids.insert(p.settings.id).second) << "duplicate " << p.settings.id;
        EXPECT_EQ(findBuiltinLook(p.settings.id), &p);
    }
    EXPECT_EQ(findBuiltinLook("no-such-look"), nullptr);
    EXPECT_NE(findBuiltinLook("oppenheimer"), nullptr);
    EXPECT_NE(findBuiltinLook("dark-green"), nullptr);
}

TEST(Looks, OnANeutralGradeTheLookIsExact) {
    for (const auto& p : builtinLooks()) {
        const ColorParams c = applyLook(ColorParams{}, p.settings);
        const LookSettings& l = p.settings;
        EXPECT_DOUBLE_EQ(c.exposure, l.exposure) << l.id;
        EXPECT_DOUBLE_EQ(c.contrast, l.contrast) << l.id;
        if (l.contrast != 0) EXPECT_DOUBLE_EQ(c.pivot, l.pivot) << l.id;
        EXPECT_DOUBLE_EQ(c.saturation, l.saturation) << l.id;
        EXPECT_DOUBLE_EQ(c.temperature, l.temperature) << l.id;
        EXPECT_DOUBLE_EQ(c.tint, l.tint) << l.id;
        EXPECT_EQ(c.lift, l.lift) << l.id;
        EXPECT_EQ(c.gamma, l.gammaWheel) << l.id;
        EXPECT_EQ(c.gain, l.gain) << l.id;
        EXPECT_EQ(c.offset, l.offset) << l.id;
        EXPECT_EQ(c.curves, l.curves) << l.id;
    }
}

TEST(Looks, AmountZeroKeepsTheCorrectionAndHalfIsBetween) {
    ColorParams base;
    base.exposure = 0.3;
    base.temperature = -0.2;
    base.lift = {0.1, 0.05, 0.0};
    base.curves[0] = {{0, 0.05}, {1, 1}};
    LookSettings look = findBuiltinLook("teal-orange")->settings;
    look.amount = 0.0;
    EXPECT_EQ(applyLook(base, look), base);
    look.amount = 0.5;
    const ColorParams half = applyLook(base, look);
    const LookSettings& full = findBuiltinLook("teal-orange")->settings;
    EXPECT_NEAR(half.lift.x, base.lift.x + full.lift.x * 0.5, 1e-9);
    EXPECT_NEAR(half.gain.y, full.gain.y * 0.5, 1e-9);
    EXPECT_NEAR(half.colorBoost, full.colorBoost * 0.5, 1e-9);
    EXPECT_EQ(half.curves[0], base.curves[0]);  // the look has no curve
}

TEST(Looks, ScaledCurvesMoveTowardTheDiagonal) {
    LookSettings l;
    l.id = "x";
    l.curves[0] = {{0, 0.2}, {0.5, 0.7}, {1, 0.8}};
    const LookSettings s = scaleLook(l, 0.5);
    EXPECT_NEAR(s.curves[0][0].y, 0.1, 1e-12);
    EXPECT_NEAR(s.curves[0][1].y, 0.6, 1e-12);
    EXPECT_NEAR(s.curves[0][2].y, 0.9, 1e-12);
    EXPECT_DOUBLE_EQ(s.curves[0][1].x, 0.5);
}

TEST(Looks, ContrastOnContrastEqualsTheTwoInSequence) {
    ColorParams base;
    base.contrast = 0.2;
    base.pivot = 0.4;
    LookSettings l;
    l.id = "c";
    l.contrast = 0.15;
    l.pivot = 0.55;
    const ColorParams c = applyLook(base, l);
    for (double x : {0.0, 0.1, 0.3, 0.5, 0.8, 1.0}) {
        const double sequential = ((x - 0.4) * 1.2 + 0.4 - 0.55) * 1.15 + 0.55;
        const double combined = (x - c.pivot) * (1 + c.contrast) + c.pivot;
        EXPECT_NEAR(combined, sequential, 1e-9) << x;
    }
}

TEST(Looks, CurvesComposeLookAfterCorrection) {
    ColorParams base;
    base.curves[0] = {{0, 0}, {0.5, 0.6}, {1, 1}};
    LookSettings l;
    l.id = "k";
    l.curves[0] = {{0, 0.05}, {0.5, 0.45}, {1, 0.95}};
    const ColorParams c = applyLook(base, l);
    for (double x = 0; x <= 1.0; x += 0.05) {
        const double expected = evaluateCurve(l.curves[0], evaluateCurve(base.curves[0], x));
        EXPECT_NEAR(evaluateCurve(c.curves[0], x), expected, 2e-3) << x;
    }
}

TEST(Looks, SaturationMultipliesAndHueWraps) {
    ColorParams base;
    base.saturation = -0.5;
    base.hue = 0.8;
    LookSettings l;
    l.id = "s";
    l.saturation = -0.5;
    l.hue = 0.4;
    const ColorParams c = applyLook(base, l);
    EXPECT_NEAR(c.saturation, -0.75, 1e-12);  // 0.5 × 0.5 = 0.25 of the color left
    EXPECT_NEAR(c.hue, -0.8, 1e-12);          // 0.8 + 0.4 = 1.2 turns past 180° to −144°
}

TEST(Looks, BlackAndWhiteLooksHaveNoColor) {
    for (const char* id : {"oppenheimer-bw", "noir"}) {
        for (const auto& px : grade(lookOnly(id))) {
            EXPECT_NEAR(px[0], px[1], 1) << id;
            EXPECT_NEAR(px[1], px[2], 1) << id;
        }
    }
}

TEST(Looks, DarkGreenTurnsNeutralsGreen) {
    const auto g = grade(lookOnly("dark-green"));
    const auto& gray = g[index("gray")];
    EXPECT_GT(gray[1], gray[0] + 15);
    EXPECT_GT(gray[1], gray[2] + 10);
    const auto& light = g[index("light")];
    EXPECT_GT(light[1], light[0] + 10);
}

TEST(Looks, TealAndOrangeSplitsShadowsAndSkin) {
    const auto g = grade(lookOnly("teal-orange"));
    const auto& shadow = g[index("shadow")];
    EXPECT_GT(shadow[2], shadow[0] + 10);  // teal shadows
    const auto& skin = g[index("skin")];
    EXPECT_GT(skin[0] - skin[2], 209 - 120);  // warmer skin than the source
}

TEST(Looks, OppenheimerIsWarmWithCoolShadows) {
    const auto g = grade(lookOnly("oppenheimer"));
    const auto& shadow = g[index("shadow")];
    EXPECT_GE(shadow[2], shadow[0] + 3);
    const auto& light = g[index("light")];
    EXPECT_GT(light[0], light[2] + 5);
    const auto& skin = g[index("skin")];
    EXPECT_GT(skin[0] - skin[2], 209 - 120);
}

TEST(Looks, ColorLooksKeepSkinBelievableAndDetailInShadows) {
    for (const auto& p : builtinLooks()) {
        if (p.category == "Black & White") continue;
        const auto g = grade(applyLook(ColorParams{}, p.settings));
        // Skin stays warm: red leads; except in the night looks, where cool
        // shadows may tint dark skin slightly blue, also green > blue.
        const bool night = p.settings.id == "neon-night" || p.settings.id == "moonlight-blue";
        for (const char* name : {"skin", "darkSkin"}) {
            const auto& s = g[index(name)];
            EXPECT_GT(s[0], s[1]) << p.settings.id << " " << name;
            EXPECT_GT(s[0], s[2]) << p.settings.id << " " << name;
            if (!night || std::string(name) == "skin") EXPECT_GT(s[1], s[2]) << p.settings.id << " " << name;
        }
        const auto& shadow = g[index("shadow")];
        EXPECT_GT(shadow[0] + shadow[1] + shadow[2], 3 * 6) << p.settings.id << ": 15 % gray crushed";
        const auto& white = g[index("white")];
        EXPECT_GT(white[0] + white[1] + white[2], 3 * 185) << p.settings.id << ": whites too dark";
    }
}

TEST(Looks, JsonRoundTripAndLenientRead) {
    LookSettings l = findBuiltinLook("oppenheimer")->settings;
    l.amount = 0.65;
    const LookSettings back = timeline::lookFromJson(timeline::toJson(l));
    EXPECT_EQ(back, l);

    timeline::ColorAdjustments c;
    c.look = l;
    timeline::Clip clip;
    clip.color = c;
    const auto json = timeline::toJson(clip);
    ASSERT_TRUE(json["color"].contains("look"));
    const auto parsed = timeline::clipFromJson(json, "clip");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message();
    EXPECT_EQ(parsed->color.look, l);

    // Old files have no look; bad values fall back to neutral.
    EXPECT_TRUE(timeline::lookFromJson(json::Json::object()).isNone());
    const auto bad = timeline::lookFromJson(json::Json{{"id", "x"}, {"amount", 7}, {"contrast", "high"}, {"lift", 3}});
    EXPECT_EQ(bad.id, "x");
    EXPECT_DOUBLE_EQ(bad.amount, 1.0);
    EXPECT_DOUBLE_EQ(bad.contrast, 0.0);
    EXPECT_TRUE(bad.lift.isIdentity());
}

TEST(Looks, GradeAtAppliesTheClipLook) {
    timeline::ColorAdjustments c;
    c.exposure = 0.2;
    c.look = findBuiltinLook("golden-hour")->settings;
    c.look.amount = 0.5;
    const ColorParams with = gradeAt(c, Time::zero());
    const ColorParams without = gradeAt(c, Time::zero(), false);
    EXPECT_DOUBLE_EQ(without.exposure, 0.2);
    EXPECT_DOUBLE_EQ(without.temperature, 0.0);
    EXPECT_NEAR(with.temperature, c.look.temperature * 0.5, 1e-12);
    EXPECT_NEAR(with.exposure, 0.2 + c.look.exposure * 0.5, 1e-12);
}

// ---- HSL curves (Resolve's Hue vs Hue / Sat / Lum, Lum vs Sat, Sat vs Sat, Sat vs Lum)

namespace {
ColorParams hslGrade(int kind, std::vector<timeline::Vec2> pts) {
    ColorParams c;
    c.hsl[static_cast<std::size_t>(kind)] = std::move(pts);
    return c;
}
QColor graded(const ColorParams& c, QColor in) {
    QImage img(1, 1, QImage::Format_RGB32);
    img.setPixelColor(0, 0, in);
    applyColor(img, c);
    return img.pixelColor(0, 0);
}
double saturationOf(QColor c) {
    const double mx = std::max({c.redF(), c.greenF(), c.blueF()});
    const double mn = std::min({c.redF(), c.greenF(), c.blueF()});
    return mx > 0 ? (mx - mn) / mx : 0;
}
}  // namespace

TEST(HslCurves, NeutralTablesChangeNothing) {
    const HslTables t = hslTables(ColorParams{});
    for (const auto& table : t)
        for (auto v : table) EXPECT_EQ(v, 128);
    for (double r : {0.0, 0.3, 0.8})
        for (double g : {0.1, 0.5, 1.0}) {
            const auto o = applyHsl(t, r, g, 0.4);
            EXPECT_NEAR(o[0], r, 1e-9);
            EXPECT_NEAR(o[1], g, 1e-9);
            EXPECT_NEAR(o[2], 0.4, 1e-9);
        }
    EXPECT_TRUE(ColorParams{}.hslIsIdentity());
}

TEST(HslCurves, HueCurvesWrapAroundRed) {
    // A point at red pulled up, the rest neutral: red's neighbours on both
    // sides of 0 / 1 rise smoothly.
    const std::vector<timeline::Vec2> pts{{0.0, 0.8}, {0.25, 0.5}, {0.75, 0.5}};
    EXPECT_NEAR(evaluateHslCurve(pts, 0.0, true), 0.8, 1e-9);
    EXPECT_NEAR(evaluateHslCurve(pts, 0.5, true), 0.5, 1e-9);
    EXPECT_GT(evaluateHslCurve(pts, 0.95, true), 0.55);  // just below 1 = just left of red
    EXPECT_GT(evaluateHslCurve(pts, 0.05, true), 0.55);
    EXPECT_NEAR(evaluateHslCurve(pts, 1.0, true), 0.8, 1e-9);
    EXPECT_NEAR(evaluateHslCurve({}, 0.3, true), 0.5, 1e-12);
}

TEST(HslCurves, HueVsSatDesaturatesOnlyTheChosenHue) {
    // Greens to gray, everything else untouched (a classic "kill the green cast" move).
    const ColorParams c = hslGrade(timeline::kHueVsSat, {{0.0, 0.5}, {0.2, 0.5}, {0.333, 0.0}, {0.46, 0.5}, {1.0, 0.5}});
    const QColor green = graded(c, QColor(60, 170, 70));
    const QColor red = graded(c, QColor(200, 50, 40));
    const QColor blue = graded(c, QColor(40, 70, 200));
    EXPECT_LT(saturationOf(green), 0.05);
    EXPECT_NEAR(saturationOf(red), saturationOf(QColor(200, 50, 40)), 0.02);
    EXPECT_NEAR(saturationOf(blue), saturationOf(QColor(40, 70, 200)), 0.02);
    EXPECT_EQ(graded(c, QColor(128, 128, 128)), QColor(128, 128, 128));  // grays stay gray
}

TEST(HslCurves, HueVsHueShiftsRedTowardOrange) {
    const ColorParams c = hslGrade(timeline::kHueVsHue, {{0.0, 0.55}, {0.15, 0.5}, {0.85, 0.5}});
    const QColor in(200, 40, 40);
    const QColor out = graded(c, in);
    // +0.047 turn (17°) at red: the hue moves toward orange by about that much.
    EXPECT_NEAR(qualifierAxes(out.redF(), out.greenF(), out.blueF())[0], 0.045, 0.012);
    EXPECT_LT(out.blue(), in.blue() - 20);
    EXPECT_GT(out.green(), in.green());
    EXPECT_EQ(graded(c, QColor(40, 60, 200)), QColor(40, 60, 200));  // blue is far from red
}

TEST(HslCurves, LumVsSatAndSatVsLum) {
    // Desaturate the shadows only.
    const ColorParams shadows = hslGrade(timeline::kLumVsSat, {{0.0, 0.0}, {0.25, 0.0}, {0.5, 0.5}, {1.0, 0.5}});
    EXPECT_LT(saturationOf(graded(shadows, QColor(60, 20, 20))), 0.1);
    EXPECT_NEAR(saturationOf(graded(shadows, QColor(250, 180, 170))), saturationOf(QColor(250, 180, 170)), 0.03);
    // Darken saturated colors only.
    const ColorParams darken = hslGrade(timeline::kSatVsLum, {{0.0, 0.5}, {0.5, 0.5}, {1.0, 0.2}});
    const QColor vivid = graded(darken, QColor(220, 30, 30));
    EXPECT_LT(vivid.red(), 200);
    EXPECT_EQ(graded(darken, QColor(150, 150, 150)), QColor(150, 150, 150));
}

TEST(HslCurves, LooksScaleTowardNeutralAndCurvesCombine) {
    LookSettings l;
    l.id = "h";
    l.hslCurves[timeline::kHueVsSat] = {{0.0, 0.5}, {0.33, 0.1}, {0.66, 0.5}};
    const LookSettings half = scaleLook(l, 0.5);
    EXPECT_NEAR(half.hslCurves[timeline::kHueVsSat][1].y, 0.3, 1e-12);
    ColorParams base;
    base.hsl[timeline::kHueVsSat] = {{0.0, 0.5}, {0.33, 0.25}, {0.66, 0.5}};  // ×0.5 at green
    const ColorParams both = applyLook(base, l);                              // then ×0.2
    EXPECT_NEAR(evaluateHslCurve(both.hsl[timeline::kHueVsSat], 0.33, true), 2 * 0.25 * 0.1, 0.02);
}

TEST(HslCurves, JsonRoundTrip) {
    timeline::Clip clip;
    clip.color.hslCurves[timeline::kHueVsHue] = {{0.1, 0.6}, {0.5, 0.5}};
    clip.color.hslCurves[timeline::kSatVsLum] = {{0.0, 0.5}, {1.0, 0.3}};
    timeline::ColorAdjustments::Node n;
    n.id = "n2";
    n.grade.hslCurves[timeline::kHueVsSat] = {{0.0, 0.5}, {0.3, 0.9}};
    clip.color.nodes.push_back(n);
    const auto back = timeline::clipFromJson(timeline::toJson(clip), "clip");
    ASSERT_TRUE(back.has_value()) << back.error().message();
    EXPECT_EQ(back->color.hslCurves, clip.color.hslCurves);
    EXPECT_EQ(back->color.nodes[0].grade.hslCurves, n.grade.hslCurves);
    const ColorParams p = gradeAt(clip.color, Time::zero());
    EXPECT_FALSE(p.hsl[timeline::kHueVsHue].empty());
    EXPECT_TRUE(p.hsl[timeline::kLumVsSat].empty());
}
