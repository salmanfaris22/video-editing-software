// Color grading: .cube LUTs, built-in camera log conversions, color wheels,
// and the camera background blur (with a stand-in person segmenter).

#include "editor/ColorGrading.h"
#include "editor/Compositor.h"

#include <QPainter>

#include <gtest/gtest.h>

#include <set>

#include <cmath>
#include <string>

using namespace lectern;
using namespace lectern::editor;

namespace {

/// A .cube LUT of `size` computing `f` on [0,1]³.
std::string cube(int size, const std::function<std::array<double, 3>(double, double, double)>& f) {
    std::string s = "# test LUT\nTITLE \"test\"\nLUT_3D_SIZE " + std::to_string(size) + "\n";
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                const auto v = f(r / double(size - 1), g / double(size - 1), b / double(size - 1));
                s += std::to_string(v[0]) + " " + std::to_string(v[1]) + " " + std::to_string(v[2]) + "\n";
            }
        }
    }
    return s;
}

QImage solid(QColor c, int w = 8, int h = 8) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    return img;
}

QColor graded(QColor c, const ColorParams& params, const Lut3D* lut = nullptr) {
    QImage img = solid(c);
    applyColor(img, params, lut);
    return img.pixelColor(4, 4);
}

}  // namespace

TEST(CubeLut, ParsesAndInterpolatesTrilinearly) {
    // Trilinear interpolation reproduces any linear map exactly.
    auto lut = parseCubeLut(cube(3, [](double r, double g, double b) {
        return std::array<double, 3>{0.5 * r, 0.25 + 0.5 * g, 1.0 - b};
    }));
    ASSERT_TRUE(lut) << lut.error().toString();
    EXPECT_EQ(lut->size, 3);
    EXPECT_EQ(lut->title, "test");
    const auto v = lut->sample(0.3f, 0.6f, 0.9f);
    EXPECT_NEAR(v[0], 0.15f, 1e-5f);
    EXPECT_NEAR(v[1], 0.55f, 1e-5f);
    EXPECT_NEAR(v[2], 0.10f, 1e-5f);
    // Outside the domain: clamped.
    EXPECT_NEAR(lut->sample(2.0f, -1.0f, 0.0f)[0], 0.5f, 1e-5f);
    EXPECT_NEAR(lut->sample(2.0f, -1.0f, 0.0f)[1], 0.25f, 1e-5f);

    // A 1D LUT and a custom domain.
    auto oneD = parseCubeLut("LUT_1D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n0 0 0\n1 1 1\n");
    ASSERT_TRUE(oneD) << oneD.error().toString();
    EXPECT_NEAR(oneD->sample(1.0f, 0.5f, 2.0f)[0], 0.5f, 1e-4f);
    EXPECT_NEAR(oneD->sample(1.0f, 0.5f, 2.0f)[1], 0.25f, 1e-4f);
    EXPECT_NEAR(oneD->sample(1.0f, 0.5f, 2.0f)[2], 1.0f, 1e-4f);

    EXPECT_FALSE(parseCubeLut("0 0 0\n1 1 1\n"));                         // no size
    EXPECT_FALSE(parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n"));                 // too few entries
    EXPECT_FALSE(parseCubeLut("LUT_3D_SIZE 2\n0 0 zero\n"));              // not a number
    EXPECT_FALSE(parseCubeLut("LUT_3D_SIZE 2\nDOMAIN_MIN 1 1 1\nDOMAIN_MAX 0 0 0\n"));
}

TEST(CubeLut, AppliesWithAnAdjustableAmount) {
    auto invert = parseCubeLut(cube(2, [](double r, double g, double b) { return std::array<double, 3>{1 - r, 1 - g, 1 - b}; }));
    ASSERT_TRUE(invert);
    ColorParams p;
    p.lut = "test";
    p.lutAmount = 1.0;
    EXPECT_EQ(graded(QColor(200, 100, 0), p, &*invert), QColor(55, 155, 255));
    p.lutAmount = 0.5;
    const QColor half = graded(QColor(200, 100, 0), p, &*invert);
    EXPECT_NEAR(half.red(), 128, 1);
    EXPECT_NEAR(half.green(), 128, 1);
    EXPECT_NEAR(half.blue(), 128, 1);
    p.lutAmount = 0.0;
    EXPECT_EQ(graded(QColor(200, 100, 0), p, &*invert), QColor(200, 100, 0));
}

TEST(CameraLog, CurvesDecodeMiddleGrayAndBlack) {
    // Published middle-gray code values of each log curve → 18 % reflectance.
    EXPECT_NEAR(decodeLog("s-log3", 420.0 / 1023.0), 0.18, 1e-6);
    EXPECT_NEAR(decodeLog("s-log3", 95.0 / 1023.0), 0.0, 1e-6);
    EXPECT_NEAR(decodeLog("v-log", 0.42331), 0.18, 1e-4);
    EXPECT_NEAR(decodeLog("v-log", 0.125), 0.0, 1e-9);
    EXPECT_NEAR(decodeLog("logc3", 0.391007), 0.18, 1e-4);
    EXPECT_NEAR(decodeLog("apple-log", 0.488271), 0.18, 1e-4);
    // Increasing everywhere, and continuous where the toe meets the log segment.
    for (const char* curve : {"s-log3", "v-log", "logc3", "apple-log"}) {
        double previous = decodeLog(curve, 0.0);
        for (int i = 1; i <= 1000; ++i) {
            const double v = decodeLog(curve, i / 1000.0);
            EXPECT_GT(v, previous) << curve << " at " << i;
            previous = v;
        }
    }
    const std::pair<const char*, double> cuts[] = {
        {"s-log3", 171.2102946929 / 1023.0}, {"v-log", 0.181}, {"logc3", 5.367655 * 0.010591 + 0.092809},
        {"apple-log", 47.28711236 * (0.01 + 0.05641088) * (0.01 + 0.05641088)}};
    for (const auto& [curve, cut] : cuts) {
        EXPECT_NEAR(decodeLog(curve, cut - 1e-9), decodeLog(curve, cut + 1e-9), 2e-5) << curve;
    }
}

TEST(CameraLog, GamutMatricesFromPrimariesMatchThePublishedOnes) {
    // ITU-R BT.2087: linear BT.2020 → BT.709.
    const Matrix3 m = gamutToRec709("rec2020");
    const Matrix3 want{{{1.6605, -0.5876, -0.0728}, {-0.1246, 1.1329, -0.0083}, {-0.0182, -0.1006, 1.1187}}};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) EXPECT_NEAR(m[i][j], want[i][j], 2e-4) << i << "," << j;
    }
    // Every conversion keeps white white.
    for (const char* gamut : {"rec2020", "s-gamut3.cine", "v-gamut", "awg3"}) {
        const Matrix3 g = gamutToRec709(gamut);
        for (std::size_t i = 0; i < 3; ++i) EXPECT_NEAR(g[i][0] + g[i][1] + g[i][2], 1.0, 1e-9) << gamut;
    }
}

TEST(CameraLog, BuiltinConversionsTurnLogGrayIntoNormalGray) {
    ASSERT_EQ(builtinLuts().size(), 4u);
    const auto slog = builtinLut("builtin:s-log3");
    ASSERT_TRUE(slog);
    EXPECT_FALSE(builtinLut("builtin:nope"));
    // Middle gray in S-Log3 is 41 %: on a normal display it should sit around 40–48 %, neutral.
    const float gray = 420.0f / 1023.0f;
    const auto out = slog->sample(gray, gray, gray);
    EXPECT_GT(out[0], 0.40f);
    EXPECT_LT(out[0], 0.48f);
    EXPECT_NEAR(out[0], out[1], 0.002f);
    EXPECT_NEAR(out[1], out[2], 0.002f);
    // Darker than the flat log picture in the shadows, brighter in the highlights: contrast restored.
    EXPECT_LT(slog->sample(0.2f, 0.2f, 0.2f)[1], 0.2f);
    EXPECT_GT(slog->sample(0.6f, 0.6f, 0.6f)[1], 0.6f);
    LutCache cache;
    EXPECT_EQ(cache.get("builtin:s-log3", {}), slog);
    EXPECT_FALSE(cache.get("missing.cube", "/nonexistent"));
}

TEST(ColorWheels, LiftGammaGainMoveShadowsMidsAndHighlights) {
    ColorParams p;
    p.lift.master = 0.4;  // shadows up, white stays
    EXPECT_GT(graded(Qt::black, p).red(), 15);
    EXPECT_EQ(graded(Qt::white, p), QColor(Qt::white));

    p = {};
    p.gain.master = -0.4;  // highlights down, black stays
    EXPECT_LT(graded(Qt::white, p).red(), 220);
    EXPECT_EQ(graded(Qt::black, p), QColor(Qt::black));

    p = {};
    p.gamma.master = 0.5;  // mids up, ends fixed
    EXPECT_GT(graded(QColor(128, 128, 128), p).red(), 150);
    EXPECT_EQ(graded(Qt::black, p), QColor(Qt::black));
    EXPECT_EQ(graded(Qt::white, p), QColor(Qt::white));

    // Pushing the lift puck toward blue (Cb) tints the shadows blue; toward red (Cr) red.
    p = {};
    p.lift.x = 1.0;
    QColor c = graded(QColor(30, 30, 30), p);
    EXPECT_GT(c.blue(), c.red() + 20);
    p.lift = {0, 1.0, 0};
    c = graded(QColor(30, 30, 30), p);
    EXPECT_GT(c.red(), c.blue() + 20);
    // The chroma of a wheel has no brightness of its own.
    const auto chroma = wheelChroma({0.3, -0.7, 0});
    EXPECT_NEAR(0.2126 * chroma[0] + 0.7152 * chroma[1] + 0.0722 * chroma[2], 0.0, 1e-3);
}

TEST(BackgroundBlur, KeepsThePersonSharpAndSoftensTheRest) {
    // A fine checkerboard; the "person" is the central square.
    QImage checker(200, 200, QImage::Format_RGB32);
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) checker.setPixelColor(x, y, ((x / 2 + y / 2) % 2) ? Qt::white : Qt::black);
    }
    setPersonSegmenter([](const QImage& image) {
        QImage mask(image.size(), QImage::Format_Grayscale8);
        mask.fill(0);
        QPainter p(&mask);
        p.fillRect(QRect(image.width() / 4, image.height() / 4, image.width() / 2, image.height() / 2), Qt::white);
        return mask;
    });
    ASSERT_TRUE(hasPersonSegmenter());

    RenderPlan plan;
    plan.width = 200;
    plan.height = 200;
    VisualLayer l;
    l.role = "camera";
    l.fill = true;
    l.box = {0, 0, 1, 1};
    l.backgroundBlur = 1.0;
    plan.layers.push_back(l);
    QImage out(200, 200, QImage::Format_RGB32);
    Compositor compositor;
    compositor.render(plan, out, [&](const VisualLayer&, QSizeF) { return checker; });
    setPersonSegmenter({});

    auto contrast = [&](QRect area) {  // mean absolute difference of horizontal neighbours
        double sum = 0;
        int n = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x < area.right(); ++x) {
                sum += std::abs(out.pixelColor(x, y).lightness() - out.pixelColor(x + 1, y).lightness());
                ++n;
            }
        }
        return sum / n;
    };
    EXPECT_GT(contrast(QRect(80, 80, 40, 40)), 100);  // the person: still a crisp checkerboard
    EXPECT_LT(contrast(QRect(5, 5, 30, 30)), 10);      // the background: smooth gray
}

// ---- Input color (Phase 1.2) ---------------------------------------------------

TEST(InputColor, ResolvesFromOverrideAndFileTags) {
    EXPECT_TRUE(resolveInputColor("auto", "", "").isIdentity());  // untagged = Rec.709
    EXPECT_TRUE(resolveInputColor("auto", "bt709", "bt709").isIdentity());
    EXPECT_EQ(resolveInputColor("auto", "arib-std-b67", "bt2020"), (InputColor{Transfer::Hlg, Primaries::Bt2020}));
    EXPECT_EQ(resolveInputColor("auto", "smpte2084", "bt2020"), (InputColor{Transfer::Pq, Primaries::Bt2020}));
    EXPECT_EQ(resolveInputColor("auto", "iec61966-2-1", "smpte432"), (InputColor{Transfer::Srgb, Primaries::DisplayP3}));
    // An override wins over the tags.
    EXPECT_TRUE(resolveInputColor("rec709", "smpte2084", "bt2020").isIdentity());
    EXPECT_EQ(resolveInputColor("rec2020-hlg", "", ""), (InputColor{Transfer::Hlg, Primaries::Bt2020}));
}

TEST(InputColor, Rec709AndSrgbPassThroughUnchanged) {
    for (const InputColor c : {InputColor{}, InputColor{Transfer::Srgb, Primaries::Bt709}}) {
        const auto out = convertInputColor(c, 0.2, 0.5, 0.9);
        EXPECT_DOUBLE_EQ(out[0], 0.2);
        EXPECT_DOUBLE_EQ(out[1], 0.5);
        EXPECT_DOUBLE_EQ(out[2], 0.9);
    }
}

TEST(InputColor, WideGamutWhiteAndGrayStayNeutral) {
    for (const InputColor c : {InputColor{Transfer::Srgb, Primaries::DisplayP3}, InputColor{Transfer::Bt709, Primaries::Bt2020}}) {
        for (double v : {0.25, 0.5, 1.0}) {
            const auto out = convertInputColor(c, v, v, v);
            EXPECT_NEAR(out[0], out[1], 2e-3) << v;
            EXPECT_NEAR(out[1], out[2], 2e-3) << v;
        }
        EXPECT_NEAR(convertInputColor(c, 1, 1, 1)[0], 1.0, 2e-3);  // white stays white
    }
    // Display P3 red is more saturated than Rec.709 can show: it clips at red, not into other hues.
    const auto red = convertInputColor({Transfer::Srgb, Primaries::DisplayP3}, 1, 0, 0);
    EXPECT_NEAR(red[0], 1.0, 1e-6);
    EXPECT_LT(red[1], 0.05);
    EXPECT_LT(red[2], 0.05);
    // Gamut matrices keep white (rows sum to 1).
    for (const Primaries p : {Primaries::DisplayP3, Primaries::Bt2020}) {
        const Matrix3 m = gamutToBt709(p);
        for (const auto& row : m) EXPECT_NEAR(row[0] + row[1] + row[2], 1.0, 1e-6);
    }
}

TEST(InputColor, PqMapsReferenceWhiteToSdrWhiteAndRollsOffHighlights) {
    const InputColor pq{Transfer::Pq, Primaries::Bt2020};
    // PQ 0.58 ≈ 203 nits (BT.2408 reference white) → about SDR white after the knee.
    const auto white = convertInputColor(pq, 0.58, 0.58, 0.58);
    EXPECT_GT(white[0], 0.9);
    EXPECT_LE(white[0], 1.0);
    // 100 nits (PQ ≈ 0.508) is about half of reference white in light: mid-gray-ish, not black or clipped.
    const auto hundred = convertInputColor(pq, 0.508, 0.508, 0.508)[0];
    EXPECT_GT(hundred, 0.65);
    EXPECT_LT(hundred, 0.85);
    // Brighter input is never darker, and 10,000 nits does not exceed 1.
    double last = 0;
    for (double e = 0.0; e <= 1.0001; e += 0.02) {
        const double v = convertInputColor(pq, e, e, e)[0];
        EXPECT_GE(v + 1e-9, last) << e;
        EXPECT_LE(v, 1.0);
        last = v;
    }
    EXPECT_NEAR(convertInputColor(pq, 0, 0, 0)[0], 0.0, 1e-9);
}

TEST(InputColor, HlgIsContinuousAndMapsItsNominalWhiteNearSdrWhite) {
    const InputColor hlg{Transfer::Hlg, Primaries::Bt2020};
    // Continuous across the curve's join at 0.5.
    const double below = convertInputColor(hlg, 0.4999, 0.4999, 0.4999)[0];
    const double above = convertInputColor(hlg, 0.5001, 0.5001, 0.5001)[0];
    EXPECT_NEAR(below, above, 2e-3);
    // HLG 0.75 is the nominal diffuse white (~203 nits on a 1000-nit display).
    const double white = convertInputColor(hlg, 0.75, 0.75, 0.75)[0];
    EXPECT_GT(white, 0.88);
    EXPECT_LE(white, 1.0);
    // Peak (1.0) rolls off below 1 instead of clipping hard; still brighter than white.
    const double peak = convertInputColor(hlg, 1, 1, 1)[0];
    EXPECT_GT(peak, white);
    EXPECT_LE(peak, 1.0);
}

TEST(InputColor, ConvertsTenBitImagesKeepingPrecision) {
    // A smooth 10-bit ramp converted to 8 bits keeps (nearly) every step: no banding from an 8-bit detour.
    QImage ramp(1024, 1, QImage::Format_BGR30);
    for (int x = 0; x < 1024; ++x) reinterpret_cast<quint32*>(ramp.scanLine(0))[x] = (3u << 30) | (x << 20) | (x << 10) | x;
    applyInputColor(ramp, {Transfer::Hlg, Primaries::Bt2020});
    ASSERT_EQ(ramp.format(), QImage::Format_RGB32);
    std::set<int> levels;
    for (int x = 0; x < 1024; ++x) levels.insert(qRed(ramp.pixel(x, 0)));
    EXPECT_GT(levels.size(), 200U);  // an 8-bit-decoded HLG ramp would give far fewer distinct levels
}
