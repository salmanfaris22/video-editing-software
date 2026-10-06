// The GPU renderer against the CPU compositor (the reference): the same
// RenderPlan drawn by both must agree within a small tolerance — every
// feature the compositor draws, one scene each — and the GPU must be fast
// enough for real-time 1080p preview.

#include "editor/ColorGrading.h"
#include "editor/Compositor.h"
#include "editor/EditorFixture.h"
#include "editor/Exporter.h"
#include "media/MediaProbe.h"
#include "media/VideoReader.h"
#include "support/TestMedia.h"
#include "editor/FrameProvider.h"
#include "editor/RenderPlan.h"
#include "render/GpuRenderer.h"

#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

using namespace lectern;
using namespace lectern::editor;

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

struct Difference {
    double mean = 0;     ///< mean absolute difference per channel (0–255)
    double outliers = 0; ///< fraction of pixels with a channel off by more than 40
    int worst = 0;
};

Difference compare(const QImage& a, const QImage& b) {
    Difference d;
    long long sum = 0;
    long long far = 0;
    for (int y = 0; y < a.height(); ++y) {
        const auto* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const auto* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            const int dr = std::abs(qRed(pa[x]) - qRed(pb[x]));
            const int dg = std::abs(qGreen(pa[x]) - qGreen(pb[x]));
            const int db = std::abs(qBlue(pa[x]) - qBlue(pb[x]));
            sum += dr + dg + db;
            const int m = std::max({dr, dg, db});
            d.worst = std::max(d.worst, m);
            if (m > 40) ++far;
        }
    }
    const double pixels = static_cast<double>(a.width()) * a.height();
    d.mean = static_cast<double>(sum) / (pixels * 3);
    d.outliers = static_cast<double>(far) / pixels;
    return d;
}

struct Both {
    FrameProvider frames;
    std::unique_ptr<FrameRenderer> cpu = makeCpuRenderer();
    std::unique_ptr<FrameRenderer> gpu = render::makeGpuRenderer();
    explicit Both(const test::EditorFixture& f) : frames(f.dir.path(), false) {
        cpu->setProjectDirectory(f.dir.path());
        if (gpu) gpu->setProjectDirectory(f.dir.path());
    }
    /// Renders `p` at `t` with both; returns the difference (and keeps the images).
    Difference at(const project::Project& p, double t, QSize size = {1280, 720}) {
        frames.setProject(std::make_shared<const project::Project>(p));
        const RenderPlan plan = buildRenderPlan(p, sec(t));
        const auto images = [this](const VisualLayer& l, QSizeF box) { return frames.image(l, box); };
        cpuImage = QImage(size, QImage::Format_RGB32);
        gpuImage = QImage(size, QImage::Format_RGB32);
        cpu->render(plan, cpuImage, images);
        gpu->render(plan, gpuImage, images);
        const Difference d = compare(cpuImage, gpuImage);
        std::printf("  difference: mean %.3f, outliers %.3f %%, worst %d\n", d.mean, d.outliers * 100, d.worst);
        return d;
    }
    QImage cpuImage;
    QImage gpuImage;
};

timeline::Clip& clipOf(test::EditorFixture& f, const char* track) { return f.track(track).clips.front(); }

/// Replaces the screen and camera media with a detailed still (gradients,
/// a fine checkerboard, colored circles, text) so blur, zoom, LUT and grades
/// have something to act on.
void useDetailImage(test::EditorFixture& f) {
    QImage img(640, 360, QImage::Format_RGB32);
    QPainter p(&img);
    QLinearGradient g(0, 0, 640, 360);
    g.setColorAt(0, QColor(250, 210, 40));
    g.setColorAt(0.5, QColor(40, 120, 230));
    g.setColorAt(1, QColor(20, 20, 30));
    p.fillRect(img.rect(), g);
    for (int y = 0; y < 120; y += 8)
        for (int x = 0; x < 200; x += 8)
            if (((x + y) / 8) % 2 == 0) p.fillRect(x, 200 + y, 8, 8, Qt::white);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor colors[] = {QColor(230, 60, 60), QColor(60, 200, 90), QColor(240, 240, 240), QColor(150, 70, 220)};
    for (int i = 0; i < 4; ++i) {
        p.setBrush(colors[i]);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(300 + i * 80, 120 + (i % 2) * 90), 45, 45);
    }
    p.setPen(Qt::black);
    QFont font;
    font.setPixelSize(34);
    p.setFont(font);
    p.drawText(QRect(220, 280, 400, 60), Qt::AlignCenter, QStringLiteral("Lectern GPU 123"));
    p.end();
    ASSERT_TRUE(img.save(QString::fromStdString((f.dir / "detail.png").string())));
    project::MediaSource m;
    m.id = project::MediaId::generate();
    m.kind = project::MediaKind::Image;
    m.role = project::MediaRole::Imported;
    m.name = "Detail";
    m.path = "detail.png";
    m.info.video = project::VideoMetadata{"png", 640, 360, FrameRate(30, 1), "rgb", "", "", 0, false};
    f.project.media.push_back(m);
    clipOf(f, "Screen").media = m.id;
    clipOf(f, "Camera").media = m.id;
}

timeline::EffectInstance effect(const char* type, std::map<std::string, double> params) {
    timeline::EffectInstance e;
    e.id = timeline::EffectId::generate();
    e.type = type;
    for (const auto& [k, v] : params) e.params[k] = timeline::Animated<double>{v};
    return e;
}

#define EXPECT_CLOSE(d, meanLimit, outlierLimit)                                                                   \
    EXPECT_LT((d).mean, meanLimit) << "worst " << (d).worst << ", outliers " << (d).outliers * 100 << " %";       \
    EXPECT_LT((d).outliers, outlierLimit) << "mean " << (d).mean << ", worst " << (d).worst

class GpuRenderer : public ::testing::Test {
protected:
    void SetUp() override {
        probe_ = render::makeGpuRenderer();
        if (!probe_) GTEST_SKIP() << "no GPU backend on this platform";
        QImage img(16, 16, QImage::Format_RGB32);
        probe_->render(RenderPlan{}, img, {});
        if (probe_->name().rfind("gpu", 0) != 0) GTEST_SKIP() << "GPU unavailable: " << probe_->name();
    }
    std::unique_ptr<FrameRenderer> probe_;
};

}  // namespace

TEST_F(GpuRenderer, ScreenAndRoundedCameraMatchTheCpu) {
    test::EditorFixture f;
    Both r(f);
    const Difference d = r.at(f.project, 0.5);
    EXPECT_CLOSE(d, 1.0, 0.002);
}

TEST_F(GpuRenderer, GradientPaddingShadowBorderAndCircleMatch) {
    test::EditorFixture f;
    f.project.canvas.backgroundColor = "#204080";
    f.project.style.backgroundColor2 = "#802040";
    f.project.style.screenPadding = 0.08;
    f.project.style.screenRadius = 0.03;
    f.project.style.screenShadow = 0.8;
    f.project.style.cameraShape = "circle";
    f.project.style.cameraBorder = 0.006;
    f.project.style.cameraBorderColor = "#FFD400";
    Both r(f);
    const Difference d = r.at(f.project, 0.5);
    // Shadows are an analytic Gaussian on the GPU and a blurred bitmap on the CPU.
    EXPECT_CLOSE(d, 1.5, 0.004);
}

TEST_F(GpuRenderer, ColorGradeCurvesAndWheelsMatch) {
    test::EditorFixture f;
    useDetailImage(f);
    timeline::ColorAdjustments& c = clipOf(f, "Screen").color;
    c.exposure = timeline::Animated<double>{0.4};
    c.contrast = timeline::Animated<double>{0.25};
    c.saturation = timeline::Animated<double>{-0.4};
    c.temperature = timeline::Animated<double>{0.5};
    c.lift = {0.1, -0.05, 0.05};
    c.gain = {-0.05, 0.08, -0.1};
    Both r(f);
    const Difference d = r.at(f.project, 0.5);
    EXPECT_CLOSE(d, 1.0, 0.002);
}

TEST_F(GpuRenderer, BuiltInLutMatches) {
    test::EditorFixture f;
    useDetailImage(f);
    timeline::ColorAdjustments& c = clipOf(f, "Camera").color;
    c.lut = "builtin:apple-log";
    c.lutAmount = 0.7;
    Both r(f);
    const Difference d = r.at(f.project, 0.5);
    EXPECT_CLOSE(d, 1.5, 0.003);
}

TEST_F(GpuRenderer, BlurVignetteAndZoomMatch) {
    test::EditorFixture f;
    useDetailImage(f);
    Both r(f);
    r.at(f.project, 0.5);
    const QImage sharp = r.cpuImage;
    clipOf(f, "Screen").effects.push_back(effect(kEffectBlur, {{"amount", 0.4}}));
    clipOf(f, "Camera").effects.push_back(effect(kEffectVignette, {{"amount", 0.8}}));
    clipOf(f, "Camera").effects.push_back(effect(kEffectZoom, {{"scale", 1.6}, {"x", 0.3}, {"y", 0.6}}));
    const Difference d = r.at(f.project, 0.5);
    EXPECT_GT(compare(sharp, r.cpuImage).mean, 3.0);  // the effects really change the picture
    // Separable Gaussian on the GPU vs three box passes on the CPU.
    EXPECT_CLOSE(d, 2.0, 0.01);
}

TEST_F(GpuRenderer, TextAndSubtitlesMatchAndStayInOrder) {
    test::EditorFixture f;
    f.addText("Hello GPU", 0.0, 3.0, "title");
    {  // a second text layer at the same time, on its own track
        const auto defaults = textPresetDefaults("lower-third");
        f.project.timeline.tracks.push_back({timeline::TrackId::generate(), timeline::TrackKind::Overlay, "Text 2"});
        timeline::Clip t;
        t.id = timeline::ClipId::generate();
        t.kind = timeline::ClipKind::Text;
        t.name = "Text";
        t.range = {sec(0), sec(3)};
        t.text = timeline::TextContent{"Lower third", "lower-third", defaults.style};
        t.transform.position = timeline::Vec2{defaults.x, defaults.y};
        ASSERT_TRUE(f.project.timeline.tracks.back().insertClip(t));
    }
    f.project.timeline.tracks.push_back({timeline::TrackId::generate(), timeline::TrackKind::Subtitle, "Subtitles"});
    timeline::Clip sub;
    sub.id = timeline::ClipId::generate();
    sub.kind = timeline::ClipKind::Subtitle;
    sub.range = {sec(0), sec(3)};
    sub.subtitle = timeline::SubtitleContent{"A subtitle under everything", {}};
    ASSERT_TRUE(f.project.timeline.tracks.back().insertClip(sub));
    Both r(f);
    const Difference d = r.at(f.project, 1.5);  // past the in-animations
    EXPECT_CLOSE(d, 1.0, 0.003);
    // The second frame reuses the cached text overlay and is still right.
    const Difference again = r.at(f.project, 1.6);
    EXPECT_CLOSE(again, 1.0, 0.003);
}

TEST_F(GpuRenderer, MissingMediaPlaceholderMatches) {
    test::EditorFixture f;
    std::filesystem::remove(f.dir.path() / "camera.mkv");
    Both r(f);
    const Difference d = r.at(f.project, 0.5);
    EXPECT_CLOSE(d, 1.0, 0.003);
}

TEST_F(GpuRenderer, BackgroundBlurUsesThePersonMask) {
    // A fake segmenter that, like a real one, follows the picture: bright
    // pixels are the "person". (A mask by position would differ between the
    // renderers, which segment the source vs. the cropped, mirrored layer.)
    setPersonSegmenter([](const QImage& image) {
        const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
        QImage mask(rgb.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < rgb.height(); ++y) {
            const auto* in = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
            auto* out = mask.scanLine(y);
            for (int x = 0; x < rgb.width(); ++x) out[x] = qGray(in[x]) > 140 ? 255 : 0;
        }
        return mask;
    });
    test::EditorFixture f;
    useDetailImage(f);
    Both r(f);
    r.at(f.project, 0.5);
    const QImage sharp = r.cpuImage;
    clipOf(f, "Camera").effects.push_back(effect(kEffectBackgroundBlur, {{"amount", 0.8}}));
    const Difference d = r.at(f.project, 0.5);
    EXPECT_GT(compare(sharp, r.cpuImage).mean, 0.2);  // the background got softer
    setPersonSegmenter({});
    // The GPU segments the source image and stretches the mask; the CPU
    // segments the enlarged layer. Same result, slightly softer mask edges.
    EXPECT_CLOSE(d, 3.0, 0.01);
}

TEST_F(GpuRenderer, OtherCanvasSizesAndPortraitMatch) {
    test::EditorFixture f;
    Both r(f);
    EXPECT_CLOSE(r.at(f.project, 0.5, QSize(640, 360)), 1.0, 0.003);
    f.project.canvas.width = 1080;
    f.project.canvas.height = 1920;
    EXPECT_CLOSE(r.at(f.project, 0.5, QSize(540, 960)), 1.0, 0.003);
}

namespace {
/// Mean milliseconds per 1080p frame for `gpu` and the CPU on project `f` at 1 s.
std::pair<double, double> benchmark(test::EditorFixture& f) {
    Both r(f);
    const RenderPlan plan = buildRenderPlan(f.project, sec(1.0));
    // Decode once; every frame hands over a fresh copy, as a playing video
    // does, so the GPU uploads it each time (the copy costs both the same).
    std::map<std::string, QImage> cache;
    const auto images = [&](const VisualLayer& l, QSizeF box) {
        const std::string key = l.role;
        if (!cache.contains(key)) cache[key] = r.frames.image(l, box);
        return cache[key].copy();
    };
    r.frames.setProject(f.snapshot());
    QImage target(1920, 1080, QImage::Format_RGB32);
    auto time = [&](FrameRenderer& renderer) {
        renderer.render(plan, target, images);  // warm up
        const auto start = std::chrono::steady_clock::now();
        constexpr int kFrames = 30;
        for (int i = 0; i < kFrames; ++i) renderer.render(plan, target, images);
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kFrames;
    };
    return {time(*r.gpu), time(*r.cpu)};
}
}  // namespace

TEST_F(GpuRenderer, RendersAStyled1080pFrameFastEnoughForRealTime) {
    test::EditorFixture f({.seconds = 3.0});
    f.project.style.screenPadding = 0.05;
    f.project.style.screenRadius = 0.02;
    f.project.style.screenShadow = 0.6;
    clipOf(f, "Screen").color.contrast = timeline::Animated<double>{0.2};
    f.addText("Benchmark", 0.0, 3.0, "title");
    const auto [gpuMs, cpuMs] = benchmark(f);
    std::printf("styled 1080p frame: GPU %.2f ms, CPU %.2f ms\n", gpuMs, cpuMs);
    EXPECT_LT(gpuMs, 16.0);  // comfortably inside a 30 fps frame (and 60 fps)
}

TEST_F(GpuRenderer, PlainFramesCostLittle) {
    test::EditorFixture f({.seconds = 3.0});
    const auto [gpuMs, cpuMs] = benchmark(f);
    std::printf("plain 1080p frame: GPU %.2f ms, CPU %.2f ms\n", gpuMs, cpuMs);
    EXPECT_LT(gpuMs, 10.0);
}

TEST_F(GpuRenderer, ExportOnTheGpuMatchesTheCpuExport) {
    // Styled, graded, with text: the exporter renders NV12 straight on the GPU.
    test::EditorFixture f;
    f.project.style.screenPadding = 0.06;
    f.project.style.screenRadius = 0.03;
    f.project.style.screenShadow = 0.7;
    f.project.style.cameraShape = "circle";
    clipOf(f, "Screen").color.contrast = timeline::Animated<double>{0.3};
    f.addText("Export", 0.2, 2.0, "lower-third");
    auto exportWith = [&](bool gpu, const char* name) {
        setRendererFactory(gpu ? RendererFactory([] { return render::makeGpuRenderer(); }) : RendererFactory());
        const auto out = f.dir / name;
        auto result = exportProject(f.project, f.dir.path(),
                                    {.output = out, .width = 1280, .height = 720, .quality = "high", .hardwareEncoder = false});
        setRendererFactory({});
        EXPECT_TRUE(result) << result.error().toString();
        return out;
    };
    const auto gpuFile = exportWith(true, "gpu.mp4");
    const auto cpuFile = exportWith(false, "cpu.mp4");
    auto gpu = media::VideoReader::open(gpuFile, {.hardwareDecode = false});
    auto cpu = media::VideoReader::open(cpuFile, {.hardwareDecode = false});
    ASSERT_TRUE(gpu && cpu);
    for (double t : {0.5, 1.5, 2.5}) {
        auto a = (*gpu)->frameAt(sec(t));
        auto b = (*cpu)->frameAt(sec(t));
        ASSERT_TRUE(a && b);
        const AVFrame* fa = a->get();
        const AVFrame* fb = b->get();
        ASSERT_EQ(fa->width, fb->width);
        double sum = 0;
        for (int y = 0; y < fa->height; ++y) {
            for (int x = 0; x < fa->width; ++x) {
                sum += std::abs(fa->data[0][y * fa->linesize[0] + x] - fb->data[0][y * fb->linesize[0] + x]);
            }
        }
        const double mean = sum / (static_cast<double>(fa->width) * fa->height);
        std::printf("  export luma difference at %.1f s: %.3f\n", t, mean);
        EXPECT_LT(mean, 1.0) << "at " << t << " s";
    }
}

TEST_F(GpuRenderer, InputColorConversionMatchesTheCpu) {
    for (const char* space : {"display-p3", "rec2020-hlg", "rec2020-pq"}) {
        test::EditorFixture f;
        useDetailImage(f);
        Both r(f);
        r.at(f.project, 0.5);
        const QImage plain = r.cpuImage;
        clipOf(f, "Screen").color.inputColorSpace = space;
        clipOf(f, "Camera").color.inputColorSpace = space;
        const Difference d = r.at(f.project, 0.5);
        EXPECT_GT(compare(plain, r.cpuImage).mean, 1.0) << space << ": the conversion changes the picture";
        EXPECT_CLOSE(d, 1.0, 0.002) << space;
    }
}

TEST_F(GpuRenderer, HdrHlgRecordingIsDecodedInTenBitsAndToneMapped) {
    test::EditorFixture f;
    // An HDR (HLG, BT.2020) 10-bit clip as the screen source, tagged as the probe reports it.
    const auto file = f.dir / "hdr.mkv";
    ASSERT_TRUE(test::writeTestHdrVideo(file, {.seconds = 3.0, .luma = 600}));
    auto info = media::probeMedia(file);
    ASSERT_TRUE(info) << info.error().toString();
    ASSERT_NE(info->video(), nullptr);
    EXPECT_EQ(info->video()->video->colorTransfer, "arib-std-b67");
    EXPECT_EQ(info->video()->video->colorPrimaries, "bt2020");
    EXPECT_TRUE(info->video()->video->hdr);
    project::MediaSource m;
    m.id = project::MediaId::generate();
    m.kind = project::MediaKind::Video;
    m.role = project::MediaRole::Screen;
    m.name = "HDR";
    m.path = "hdr.mkv";
    m.info.duration = Time::fromSecondsF(3.0);
    m.info.video = project::VideoMetadata{"ffv1", 320, 180, FrameRate(30, 1), "yuv420p10le", "bt2020nc", "tv", 0, false,
                                          info->video()->video->colorPrimaries, info->video()->video->colorTransfer};
    f.project.media.push_back(m);
    clipOf(f, "Screen").media = m.id;

    const RenderPlan plan = buildRenderPlan(f.project, sec(0.5));
    const VisualLayer* screen = nullptr;
    for (const auto& l : plan.layers) if (l.role == "screen") screen = &l;
    ASSERT_TRUE(screen);
    EXPECT_EQ(screen->input, (InputColor{Transfer::Hlg, Primaries::Bt2020}));
    FrameProvider frames(f.dir.path(), false);
    frames.setProject(std::make_shared<const project::Project>(f.project));
    const QImage decoded = frames.image(*screen, QSizeF(320, 180));
    EXPECT_EQ(decoded.format(), QImage::Format_BGR30);  // 10 bits kept

    Both r(f);
    const Difference d = r.at(f.project, 0.5);
    EXPECT_CLOSE(d, 1.0, 0.003);
    // Luma 600 of 64..940 (limited range) is HLG code (600-64)/876; gray in, converted gray out.
    const double code = (600.0 - 64.0) / 876.0;
    const double expected = convertInputColor({Transfer::Hlg, Primaries::Bt2020}, code, code, code)[0] * 255.0;
    const QColor center = r.gpuImage.pixelColor(r.gpuImage.width() / 2, r.gpuImage.height() / 2);
    EXPECT_NEAR(center.red(), expected, 4.0);
    EXPECT_NEAR(center.green(), expected, 4.0);
    // Without color management the same pixel would be the raw code value (washed out).
    EXPECT_GT(std::abs(expected - code * 255.0), 20.0);
}

TEST_F(GpuRenderer, ResolvePrimariesMatchTheCpu) {
    test::EditorFixture f;
    useDetailImage(f);
    Both r(f);
    r.at(f.project, 0.5);
    const QImage plain = r.cpuImage;
    timeline::ColorAdjustments& c = clipOf(f, "Camera").color;  // the top layer in this scene
    c.contrast = timeline::Animated<double>{0.3};
    c.pivot = 0.35;
    c.shadows = timeline::Animated<double>{0.4};
    c.highlights = timeline::Animated<double>{-0.3};
    c.offset = {0.05, -0.04, 0.03};
    c.colorBoost = timeline::Animated<double>{0.6};
    c.hue = timeline::Animated<double>{0.15};
    const Difference d = r.at(f.project, 0.5);
    EXPECT_GT(compare(plain, r.cpuImage).mean, 3.0);  // the grade really changes the picture
    EXPECT_CLOSE(d, 1.0, 0.002);
}

TEST_F(GpuRenderer, CustomCurvesMatchTheCpu) {
    test::EditorFixture f;
    useDetailImage(f);
    Both r(f);
    r.at(f.project, 0.5);
    const QImage plain = r.cpuImage;
    timeline::ColorAdjustments& c = clipOf(f, "Camera").color;
    c.curves[0] = {{0, 0.05}, {0.35, 0.25}, {0.7, 0.8}, {1, 0.95}};  // S-curve on luma
    c.curves[3] = {{0, 0}, {0.5, 0.6}, {1, 1}};                        // lift blue mids
    const Difference d = r.at(f.project, 0.5);
    EXPECT_GT(compare(plain, r.cpuImage).mean, 3.0);
    EXPECT_CLOSE(d, 1.0, 0.002);
}

TEST_F(GpuRenderer, NodesWithWindowsAndQualifiersMatchTheCpu) {
    test::EditorFixture f;
    useDetailImage(f);
    Both r(f);
    r.at(f.project, 0.5);
    const QImage plain = r.cpuImage;
    timeline::ColorAdjustments& c = clipOf(f, "Camera").color;
    c.exposure = timeline::Animated<double>{0.1};  // a primary under the nodes
    // A soft rotated circle that brightens and warms, a gradient that cools
    // the top, and a qualifier that turns the green disc magenta.
    timeline::ColorAdjustments::Node spot;
    spot.id = "n1";
    spot.window.shape = "circle";
    spot.window.x = 0.55;
    spot.window.y = 0.45;
    spot.window.width = 0.35;
    spot.window.height = 0.5;
    spot.window.rotation = 25;
    spot.window.softness = 0.4;
    spot.grade.exposure = 0.6;
    spot.grade.temperature = 0.5;
    timeline::ColorAdjustments::Node sky;
    sky.id = "n2";
    sky.window.shape = "gradient";
    sky.window.height = 0.8;
    sky.window.y = 0.3;
    sky.grade.temperature = -0.6;
    sky.grade.contrast = 0.2;
    timeline::ColorAdjustments::Node green;
    green.id = "n3";
    green.qualifier = qualifierAround(60 / 255.0, 200 / 255.0, 90 / 255.0);
    green.grade.hue = 0.5;
    green.grade.saturation = 0.3;
    green.grade.curves[0] = {{0, 0}, {0.5, 0.6}, {1, 1}};
    c.nodes = {spot, sky, green};
    Difference d = r.at(f.project, 0.5);
    EXPECT_GT(compare(plain, r.cpuImage).mean, 3.0);  // the nodes really change the picture
    EXPECT_CLOSE(d, 1.2, 0.004);

    // Mirrored camera and an inverted rectangle: windows stay on the source.
    clipOf(f, "Camera").transform.flipH = true;
    c.nodes[1].window.shape = "rectangle";
    c.nodes[1].window.invert = true;
    c.nodes[1].window.rotation = -15;
    d = r.at(f.project, 0.5);
    EXPECT_CLOSE(d, 1.2, 0.004);

    // Highlight view of the qualifier node.
    clipOf(f, "Camera").color.nodes = {green};
    RenderPlan plan = buildRenderPlan(f.project, sec(0.5));
    for (auto& l : plan.layers) {
        if (!l.nodes.empty()) l.highlightNode = 0;
    }
    r.frames.setProject(std::make_shared<const project::Project>(f.project));
    const auto images = [&r](const VisualLayer& l, QSizeF box) { return r.frames.image(l, box); };
    QImage cpu(1280, 720, QImage::Format_RGB32);
    QImage gpu(1280, 720, QImage::Format_RGB32);
    r.cpu->render(plan, cpu, images);
    r.gpu->render(plan, gpu, images);
    d = compare(cpu, gpu);
    std::printf("  highlight difference: mean %.3f, outliers %.3f %%\n", d.mean, d.outliers * 100);
    EXPECT_CLOSE(d, 1.2, 0.004);
}

TEST_F(GpuRenderer, PersonAndBackgroundNodesMatchTheCpu) {
    setPersonSegmenter([](const QImage& image) {  // bright pixels are the "person"
        const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
        QImage mask(rgb.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < rgb.height(); ++y) {
            const auto* in = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
            auto* out = mask.scanLine(y);
            for (int x = 0; x < rgb.width(); ++x) out[x] = qGray(in[x]) > 140 ? 255 : 0;
        }
        return mask;
    });
    test::EditorFixture f;
    useDetailImage(f);
    Both r(f);
    r.at(f.project, 0.5);
    const QImage plain = r.cpuImage;
    timeline::ColorAdjustments& c = clipOf(f, "Camera").color;
    timeline::ColorAdjustments::Node person;
    person.id = "p";
    person.subject = "person";
    person.grade.temperature = 0.6;
    timeline::ColorAdjustments::Node background;
    background.id = "b";
    background.subject = "background";
    background.grade.saturation = -1.0;
    background.grade.exposure = -0.5;
    c.nodes = {person, background};
    const Difference d = r.at(f.project, 0.5);
    setPersonSegmenter({});
    EXPECT_GT(compare(plain, r.cpuImage).mean, 3.0);
    // As with the background blur: the GPU segments the source, the CPU the layer.
    EXPECT_CLOSE(d, 3.0, 0.01);
}

TEST_F(GpuRenderer, FilmGrainGlowHalationAndPrintMatchTheCpu) {
    test::EditorFixture f;
    useDetailImage(f);
    Both r(f);
    r.at(f.project, 0.5);
    const QImage plain = r.cpuImage;
    auto& camera = clipOf(f, "Camera");
    // Grain alone: the same hashed noise on both renderers.
    camera.effects.push_back(effect(kEffectFilmGrain, {{"amount", 0.6}, {"size", 1.5}}));
    Difference d = r.at(f.project, 0.5);
    EXPECT_GT(compare(plain, r.cpuImage).mean, 1.0);
    EXPECT_CLOSE(d, 1.0, 0.003);
    // Glow, halation and the print stage, with a vignette and grain on top.
    camera.effects.push_back(effect(kEffectGlow, {{"amount", 0.5}, {"threshold", 0.7}, {"radius", 0.5}}));
    camera.effects.push_back(effect(kEffectHalation, {{"amount", 0.7}, {"threshold", 0.75}, {"radius", 0.6}}));
    camera.effects.push_back(effect(kEffectFilmEmulation, {{"amount", 1.0}, {"stock", 0}}));
    camera.effects.push_back(effect(kEffectVignette, {{"amount", 0.5}}));
    d = r.at(f.project, 0.5);
    EXPECT_GT(compare(plain, r.cpuImage).mean, 4.0);
    // Blurs differ slightly (three box passes vs a Gaussian), as for the blur effect.
    EXPECT_CLOSE(d, 2.0, 0.01);
}

TEST_F(GpuRenderer, HslCurvesMatchTheCpu) {
    test::EditorFixture f;
    useDetailImage(f);
    Both r(f);
    r.at(f.project, 0.5);
    const QImage plain = r.cpuImage;
    timeline::ColorAdjustments& c = clipOf(f, "Camera").color;
    c.hslCurves[timeline::kHueVsSat] = {{0.0, 0.5}, {0.25, 0.5}, {0.36, 0.05}, {0.48, 0.5}};  // greens to gray
    c.hslCurves[timeline::kHueVsHue] = {{0.0, 0.58}, {0.12, 0.5}, {0.9, 0.5}};                // reds toward orange
    c.hslCurves[timeline::kLumVsSat] = {{0.0, 0.2}, {0.3, 0.5}, {1.0, 0.5}};
    timeline::ColorAdjustments::Node node;  // and a node with its own HSL curve
    node.id = "n2";
    node.window.shape = "circle";
    node.window.width = 0.5;
    node.window.height = 0.8;
    node.grade.hslCurves[timeline::kSatVsLum] = {{0.0, 0.5}, {1.0, 0.25}};
    c.nodes = {node};
    const Difference d = r.at(f.project, 0.5);
    EXPECT_GT(compare(plain, r.cpuImage).mean, 2.0);
    EXPECT_CLOSE(d, 1.0, 0.003);
}
