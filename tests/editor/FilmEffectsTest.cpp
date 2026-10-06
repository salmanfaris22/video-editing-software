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

namespace {

/// A dark and a bright half, each with ±12 levels of fixed noise.
QImage noisyHalves(int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    std::uint32_t seed = 12345;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const int v = (x < w / 2 ? 60 : 190) + static_cast<int>((seed >> 24) % 25) - 12;
            img.setPixel(x, y, qRgb(v, v, v));
        }
    }
    return img;
}

/// Standard deviation of the red channel over columns [x0, x1), rows 8…h−8.
double spread(const QImage& img, int x0, int x1) {
    double sum = 0, sumSq = 0;
    int n = 0;
    for (int y = 8; y < img.height() - 8; ++y) {
        for (int x = x0; x < x1; ++x) {
            const double v = qRed(img.pixel(x, y));
            sum += v;
            sumSq += v * v;
            ++n;
        }
    }
    const double mean = sum / n;
    return std::sqrt(std::max(0.0, sumSq / n - mean * mean));
}

double columnMean(const QImage& img, int x) {
    double sum = 0;
    for (int y = 0; y < img.height(); ++y) sum += qRed(img.pixel(x, y));
    return sum / img.height();
}

}  // namespace

TEST(NoiseReduction, SmoothsFlatAreasAndKeepsEdges) {
    const QImage noisy = noisyHalves(96, 64);  // gray: luma noise only
    QImage clean = noisy;
    denoiseImage(clean, denoiseLumaSigma(0.6), 0.0, 1, 2.0);
    EXPECT_LT(spread(clean, 6, 40), spread(noisy, 6, 40) * 0.5);
    EXPECT_LT(spread(clean, 56, 90), spread(noisy, 56, 90) * 0.5);
    // The edge between the halves stays a hard step: nothing bleeds across.
    EXPECT_NEAR(columnMean(clean, 47), 60, 3);
    EXPECT_NEAR(columnMean(clean, 48), 190, 3);
    // More amount, more smoothing; the gray stays gray.
    QImage strong = noisy;
    denoiseImage(strong, denoiseLumaSigma(1.0), denoiseChromaSigma(1.0), 1, 2.0);
    EXPECT_LT(spread(strong, 6, 40), spread(clean, 6, 40));
    const QColor gray = strong.pixelColor(20, 20);
    EXPECT_NEAR(gray.red(), gray.blue(), 1);
    EXPECT_NEAR(gray.red(), gray.green(), 1);
    // Off: nothing changes.
    QImage same = noisy;
    denoiseImage(same, 0.0, 0.0, 1, 2.0);
    EXPECT_EQ(same, noisy);
}

TEST(NoiseReduction, ChromaRemovesColorBlotchesAndKeepsLumaDetail) {
    // Gray with luma detail (stripes) and color noise of the same brightness.
    QImage img(96, 64, QImage::Format_RGB32);
    std::uint32_t seed = 7;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 96; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const int base = (x / 3) % 2 ? 150 : 110;              // 3-pixel stripes
            const int tint = static_cast<int>((seed >> 24) % 31) - 15;  // red / blue speckle
            img.setPixel(x, y, qRgb(std::clamp(base + tint, 0, 255), std::clamp(base - tint / 3, 0, 255), std::clamp(base - tint, 0, 255)));
        }
    }
    auto chroma = [](const QImage& im) {  // mean |R − B| away from the borders
        double sum = 0;
        int n = 0;
        for (int y = 8; y < 56; ++y)
            for (int x = 8; x < 88; ++x) {
                sum += std::abs(qRed(im.pixel(x, y)) - qBlue(im.pixel(x, y)));
                ++n;
            }
        return sum / n;
    };
    auto stripes = [](const QImage& im) {  // luma contrast between the stripes
        return qGray(im.pixel(31, 30)) - qGray(im.pixel(28, 30));
    };
    QImage clean = img;
    denoiseImage(clean, 0.0, denoiseChromaSigma(0.8), 1, 2.0);
    EXPECT_LT(chroma(clean), chroma(img) * 0.4);          // the speckle is gone
    EXPECT_NEAR(stripes(clean), stripes(img), 6);         // the stripes are as crisp as before
}

TEST(NoiseReduction, StepAndRadiusFollowTheCanvasHeight) {
    EXPECT_NEAR(denoiseSigmaPixels(0.5, 1080), 2.5, 1e-9);
    EXPECT_NEAR(denoiseSigmaPixels(0.5, 2160), 5.0, 1e-9);
    EXPECT_EQ(denoiseStep(denoiseSigmaPixels(0.5, 1080)), 1);
    EXPECT_EQ(denoiseStep(denoiseSigmaPixels(1.0, 2160)), 4);
    EXPECT_GE(denoiseSigmaPixels(0.0, 180), 0.8);  // small previews still filter
    EXPECT_LT(denoiseLumaSigma(0.1), denoiseLumaSigma(0.9));
    EXPECT_EQ(denoiseLumaSigma(0.0), 0.0);
    EXPECT_GT(denoiseChromaSigma(0.5), denoiseLumaSigma(0.5));  // color noise is coarser
}

TEST(Sharpen, RaisesEdgeContrastAndLeavesFlatAreas) {
    QImage soft(64, 16, QImage::Format_RGB32);
    for (int x = 0; x < 64; ++x) {
        const int v = x < 30 ? 80 : x >= 34 ? 170 : 80 + (x - 30) * 90 / 4;  // a soft edge
        for (int y = 0; y < 16; ++y) soft.setPixel(x, y, qRgb(v, v, v));
    }
    QImage crisp = soft;
    sharpenImage(crisp, sharpenStrength(0.8), 1.5);
    EXPECT_LT(qRed(crisp.pixel(29, 8)), 80 - 3);   // dark side dips
    EXPECT_GT(qRed(crisp.pixel(34, 8)), 170 + 3);  // bright side overshoots
    EXPECT_GT(qRed(crisp.pixel(33, 8)) - qRed(crisp.pixel(31, 8)), qRed(soft.pixel(33, 8)) - qRed(soft.pixel(31, 8)));
    EXPECT_EQ(qRed(crisp.pixel(4, 8)), 80);  // flat areas: untouched
    EXPECT_EQ(qRed(crisp.pixel(60, 8)), 170);
    // A flat picture does not change at all.
    QImage flat(32, 32, QImage::Format_RGB32);
    flat.fill(qRgb(120, 90, 200));
    QImage same = flat;
    sharpenImage(same, sharpenStrength(1.0), 2.0);
    EXPECT_EQ(same, flat);
}

TEST(Sharpen, CoringLeavesNoiseAndStillSharpensEdges) {
    // ±2 levels of noise on a flat area, and a soft edge further right.
    QImage img(96, 16, QImage::Format_RGB32);
    std::uint32_t seed = 3;
    for (int x = 0; x < 96; ++x) {
        for (int y = 0; y < 16; ++y) {
            seed = seed * 1664525u + 1013904223u;
            const int edge = x < 62 ? 100 : x >= 64 ? 180 : 100 + (x - 61) * 27;
            const int v = edge + (x < 48 ? static_cast<int>((seed >> 24) % 5) - 2 : 0);
            img.setPixel(x, y, qRgb(v, v, v));
        }
    }
    QImage plain = img;
    sharpenImage(plain, sharpenStrength(1.0), 1.0);
    QImage cored = img;
    sharpenImage(cored, sharpenStrength(1.0), 1.0, sharpenCoring(0.4));  // ≈ 5 levels
    double noisePlain = 0, noiseCored = 0, noiseIn = 0;
    for (int y = 2; y < 14; ++y) {
        for (int x = 4; x < 44; ++x) {
            noiseIn += std::abs(qRed(img.pixel(x, y)) - 100);
            noisePlain += std::abs(qRed(plain.pixel(x, y)) - 100);
            noiseCored += std::abs(qRed(cored.pixel(x, y)) - 100);
        }
    }
    EXPECT_GT(noisePlain, noiseIn * 1.5);   // without coring the noise is sharpened too
    EXPECT_LE(noiseCored, noiseIn * 1.05);  // with it the noise stays as it was
    EXPECT_GT(qRed(cored.pixel(64, 8)), 180 + 5);  // and the edge is still crisper
}

TEST(Sharpen, GaussianIsSymmetricAndKeepsBrightness) {
    QImage dot(31, 31, QImage::Format_RGB32);
    dot.fill(qRgb(0, 0, 0));
    dot.setPixel(15, 15, qRgb(255, 255, 255));
    gaussianBlurImage(dot, 1.5);
    EXPECT_LT(qRed(dot.pixel(15, 15)), 60);
    EXPECT_EQ(dot.pixel(14, 15), dot.pixel(16, 15));
    EXPECT_EQ(dot.pixel(15, 13), dot.pixel(15, 17));
    EXPECT_EQ(dot.pixel(13, 13), dot.pixel(17, 17));
    double total = 0;
    for (int y = 0; y < 31; ++y)
        for (int x = 0; x < 31; ++x) total += qRed(dot.pixel(x, y));
    EXPECT_NEAR(total, 255, 40);  // 8-bit rounding between the passes
}

TEST(FilmEffects, NoiseReductionAndSharpenReachTheRenderPlan) {
    test::EditorFixture f;
    auto& clip = f.track("Camera").clips.front();
    auto effect = [](const char* type, std::map<std::string, double> params) {
        timeline::EffectInstance e;
        e.id = timeline::EffectId::generate();
        e.type = type;
        for (const auto& [k, v] : params) e.params[k] = timeline::Animated<double>{v};
        return e;
    };
    clip.effects.push_back(effect(kEffectDenoise, {{"luma", 0.5}}));
    clip.effects.push_back(effect(kEffectSharpen, {{"amount", 0.4}, {"radius", 0.2}}));
    const RenderPlan plan = buildRenderPlan(f.project, Time::fromSecondsF(1.0));
    bool found = false;
    for (const VisualLayer& l : plan.layers) {
        if (l.role != "camera") continue;
        found = true;
        EXPECT_DOUBLE_EQ(l.denoiseLuma, 0.5);
        EXPECT_DOUBLE_EQ(l.denoiseChroma, 0.5);  // defaults
        EXPECT_DOUBLE_EQ(l.denoiseRadius, 0.5);
        EXPECT_DOUBLE_EQ(l.sharpen, 0.4);
        EXPECT_DOUBLE_EQ(l.sharpenRadius, 0.2);
        EXPECT_DOUBLE_EQ(l.sharpenCoring, 0.2);  // default
    }
    EXPECT_TRUE(found);
    for (const VisualLayer& l : ungraded(plan).layers) {  // bypass shows the untouched picture
        EXPECT_EQ(l.denoiseLuma, 0.0);
        EXPECT_EQ(l.denoiseChroma, 0.0);
        EXPECT_EQ(l.sharpen, 0.0);
    }
}
