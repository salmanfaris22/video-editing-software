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
