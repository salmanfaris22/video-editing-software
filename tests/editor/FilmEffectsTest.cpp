// Film and lens effects of the Color page: grain, glow, halation and film
// print emulation (CPU reference; GpuRendererTest checks the GPU against it).

#include "editor/Compositor.h"
#include "editor/Looks.h"
#include "editor/RenderPlan.h"
#include "editor/EditorFixture.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

#include <cmath>

using namespace lectern;
using namespace lectern::editor;

TEST(FilmGrain, NoiseIsDeterministicCenteredAndMovesWithTheSeed) {
    double sum = 0, sumSq = 0;
    int differ = 0;
    const int n = 200 * 200;
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            const double v = grainNoise(x + 0.5, y + 0.5, 1.5, 7);
            ASSERT_GE(v, -1.0);
            ASSERT_LE(v, 1.0);
            EXPECT_EQ(v, grainNoise(x + 0.5, y + 0.5, 1.5, 7));
            sum += v;
            sumSq += v * v;
            if (std::abs(v - grainNoise(x + 0.5, y + 0.5, 1.5, 8)) > 0.05) ++differ;
        }
    }
    EXPECT_NEAR(sum / n, 0.0, 0.02);
    EXPECT_GT(std::sqrt(sumSq / n), 0.3);  // visible, not a whisper
    EXPECT_GT(differ, n * 3 / 4);          // the next frame's grain is different
}

TEST(FilmGrain, StrongestInTheMidtones) {
    auto spread = [](int level) {
        QImage img(128, 128, QImage::Format_RGB32);
        img.fill(qRgb(level, level, level));
        addGrain(img, 1.0, 1.5, 3);
        double sumSq = 0;
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x) {
                const QColor c = img.pixelColor(x, y);
                EXPECT_EQ(c.red(), c.green());  // monochrome grain
                sumSq += std::pow(c.red() - level, 2.0);
            }
        return std::sqrt(sumSq / (128 * 128));
    };
    const double mid = spread(128);
    EXPECT_GT(mid, 6.0);
    EXPECT_GT(mid, spread(10) * 1.8);
    EXPECT_GT(mid, spread(245) * 1.8);
}

TEST(Glow, SpreadsAroundHighlightsAndHalationIsRed) {
    QImage img(120, 80, QImage::Format_RGB32);
    img.fill(qRgb(30, 30, 30));
    for (int y = 35; y < 45; ++y)
        for (int x = 55; x < 65; ++x) img.setPixel(x, y, qRgb(255, 255, 255));
    QImage glow = img;
    addGlow(glow, 1.0, 0.75, 8.0, kGlowTint);
    const QColor near = glow.pixelColor(70, 40);  // just outside the bright square
    EXPECT_GT(near.red(), 40);
    EXPECT_NEAR(near.red(), near.blue(), 2);  // white glow
    EXPECT_EQ(glow.pixelColor(5, 5), QColor(30, 30, 30));  // far away: untouched
    QImage halo = img;
    addGlow(halo, 1.0, 0.75, 8.0, kHalationTint);
    const QColor h = halo.pixelColor(70, 40);
    EXPECT_GT(h.red(), h.green() + 10);
    EXPECT_GT(h.green(), h.blue());
    // Dark pictures do not glow.
    QImage dark(64, 64, QImage::Format_RGB32);
    dark.fill(qRgb(120, 120, 120));
    QImage same = dark;
    addGlow(same, 1.0, 0.75, 8.0, kGlowTint);
    EXPECT_EQ(same, dark);
}

TEST(FilmEmulation, StocksShapeContrastAndColor) {
    const auto& stocks = filmStocks();
    ASSERT_EQ(stocks.size(), 3u);
    auto shade = [](const ColorParams& c, int v) {
        QImage img(1, 1, QImage::Format_RGB32);
        img.setPixel(0, 0, qRgb(v, v, v));
        applyColor(img, c);
        return img.pixelColor(0, 0);
    };
    const ColorParams warm = paramsOf(stocks[0].grade);
    EXPECT_GT(shade(warm, 210).red(), shade(warm, 210).blue() + 5);   // warm highlights
    EXPECT_GE(shade(warm, 40).blue(), shade(warm, 40).red());         // cool shadows
    EXPECT_GT(shade(warm, 160).green() - shade(warm, 96).green(), 64);  // more contrast in the mids
    const ColorParams soft = paramsOf(stocks[2].grade);
    EXPECT_GT(shade(soft, 0).green(), 10);  // lifted blacks
}

TEST(FilmEffects, ClipEffectsReachTheRenderPlan) {
    test::EditorFixture f;
    auto& clip = f.track("Camera").clips.front();
    auto effect = [](const char* type, std::map<std::string, double> params) {
        timeline::EffectInstance e;
        e.id = timeline::EffectId::generate();
        e.type = type;
        for (const auto& [k, v] : params) e.params[k] = timeline::Animated<double>{v};
        return e;
    };
    clip.effects.push_back(effect(kEffectFilmGrain, {{"amount", 0.5}, {"size", 2.0}}));
    clip.effects.push_back(effect(kEffectGlow, {{"amount", 0.3}}));
    clip.effects.push_back(effect(kEffectHalation, {{"amount", 0.6}, {"radius", 0.7}}));
    clip.effects.push_back(effect(kEffectFilmEmulation, {{"amount", 0.8}, {"stock", 1}}));
    clip.color.nodes.push_back(timeline::ColorAdjustments::Node{});
    clip.color.nodes.back().id = "n2";
    const RenderPlan a = buildRenderPlan(f.project, Time::fromSecondsF(1.0));
    const RenderPlan b = buildRenderPlan(f.project, Time::fromSecondsF(1.0 + 1.0 / 24));
    for (std::size_t i = 0; i < a.layers.size(); ++i) {
        const VisualLayer& l = a.layers[i];
        if (l.role != "camera") continue;
        EXPECT_DOUBLE_EQ(l.grain, 0.5);
        EXPECT_DOUBLE_EQ(l.grainSize, 2.0);
        EXPECT_DOUBLE_EQ(l.glow, 0.3);
        EXPECT_DOUBLE_EQ(l.halation, 0.6);
        EXPECT_DOUBLE_EQ(l.halationRadius, 0.7);
        ASSERT_EQ(l.nodes.size(), 2u);  // the clip's node, then the print stage over the whole picture
        EXPECT_TRUE(l.nodes[1].window.isNone());
        EXPECT_FALSE(l.nodes[1].qualifier.enabled);
        EXPECT_LT(l.nodes[1].grade.saturation, 0.0);  // cool print desaturates a little
        EXPECT_NE(l.grainSeed, b.layers[i].grainSeed);  // the grain moves every 1/24 s
    }
}
