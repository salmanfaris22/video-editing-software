#include "editor/Compositor.h"
#include "editor/EditorFixture.h"
#include "editor/FrameProvider.h"
#include "timeline/Subtitles.h"

#include <QPainter>

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::editor;

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

struct Render {
    FrameProvider frames;
    Compositor compositor;
    QImage canvas{1280, 720, QImage::Format_RGB32};
    explicit Render(const test::EditorFixture& f) : frames(f.dir.path(), false) { frames.setProject(f.snapshot()); }
    QImage& at(const project::Project& p, double t) {
        frames.setProject(std::make_shared<const project::Project>(p));
        compositor.render(buildRenderPlan(p, sec(t)), canvas,
                          [this](const VisualLayer& l, QSizeF box) { return frames.image(l, box); });
        return canvas;
    }
};

QColor pixel(const QImage& img, double fx, double fy) {
    return img.pixelColor(static_cast<int>(fx * (img.width() - 1)), static_cast<int>(fy * (img.height() - 1)));
}

}  // namespace

TEST(Compositor, DrawsScreenAndCameraWithTheirColorsShapesAndOrder) {
    test::EditorFixture f;
    Render r(f);
    const QImage& img = r.at(f.project, 0.5);  // frame 15 of both sources
    const QColor screen = test::yuv709(test::frameLevel(15), test::kScreenU, test::kScreenV);
    const QColor camera = test::yuv709(test::frameLevel(15), test::kCameraU, test::kCameraV);
    EXPECT_TRUE(test::near(pixel(img, 0.4, 0.4), screen)) << pixel(img, 0.4, 0.4).name().toStdString() << " vs "
                                                         << screen.name().toStdString();
    const NormRect cam = *layoutSlots("pip.bottom-right.rounded", 1280, 720).camera;
    const QColor inside = pixel(img, cam.x + cam.w / 2, cam.y + cam.h / 2);
    EXPECT_TRUE(test::near(inside, camera)) << inside.name().toStdString() << " vs " << camera.name().toStdString();
    // Just inside the box's corner, the rounded corner shows the screen underneath.
    const QColor corner = img.pixelColor(static_cast<int>(cam.x * 1280) + 1, static_cast<int>(cam.y * 720) + 1);
    EXPECT_FALSE(test::near(corner, camera, 30)) << corner.name().toStdString();
}

TEST(Compositor, PaddingRevealsAGradientBackgroundAndCirclesAreRound) {
    test::EditorFixture f;
    f.project.canvas.backgroundColor = "#204080";
    f.project.style.backgroundColor2 = "#802040";
    f.project.style.screenPadding = 0.1;
    f.project.style.cameraShape = "circle";
    Render r(f);
    const QImage& img = r.at(f.project, 0.5);
    EXPECT_TRUE(test::near(img.pixelColor(1, 1), QColor("#204080"), 6));
    EXPECT_TRUE(test::near(img.pixelColor(1278, 718), QColor("#802040"), 6));
    const QColor camera = test::yuv709(test::frameLevel(15), test::kCameraU, test::kCameraV);
    const VisualLayer* cam = nullptr;
    const RenderPlan plan = buildRenderPlan(f.project, sec(0.5));
    for (const auto& l : plan.layers) {
        if (l.role == "camera") cam = &l;
    }
    ASSERT_TRUE(cam && cam->circle);
    EXPECT_TRUE(test::near(pixel(img, cam->box.x + cam->box.w / 2, cam->box.y + cam->box.h / 2), camera));
    const QColor boxCorner = img.pixelColor(static_cast<int>(cam->box.x * 1280) + 3, static_cast<int>(cam->box.y * 720) + 3);
    EXPECT_FALSE(test::near(boxCorner, camera, 30));
}

TEST(Compositor, ZoomShowsTheChosenPartOfAnImage) {
    test::EditorFixture f({.camera = false});
    QImage quadrants(400, 200, QImage::Format_RGB32);
    QPainter qp(&quadrants);
    qp.fillRect(0, 0, 200, 100, QColor(220, 30, 30));    // top-left: red
    qp.fillRect(200, 0, 200, 100, QColor(30, 220, 30));  // top-right: green
    qp.fillRect(0, 100, 400, 100, QColor(30, 30, 220));  // bottom: blue
    qp.end();
    ASSERT_TRUE(quadrants.save(QString::fromStdString((f.dir / "quad.png").string())));
    // Use the image as the main "screen" content.
    project::MediaSource img;
    img.id = project::MediaId::generate();
    img.kind = project::MediaKind::Image;
    img.role = project::MediaRole::Imported;
    img.name = "Quadrants";
    img.path = "quad.png";
    img.info.video = project::VideoMetadata{"png", 400, 200, FrameRate(30, 1), "rgb", "", "", 0, false};
    f.project.media.push_back(img);
    auto& clip = f.track("Screen").clips[0];
    clip.media = img.id;
    timeline::EffectInstance zoom{timeline::EffectId::generate(), kEffectZoom, 1, true, {}};
    zoom.params["scale"] = 2.0;
    zoom.params["x"] = 0.25;
    zoom.params["y"] = 0.25;
    clip.effects.push_back(zoom);
    Render r(f);
    const QImage& out = r.at(f.project, 1.0);
    EXPECT_TRUE(test::near(pixel(out, 0.5, 0.5), QColor(220, 30, 30), 8)) << pixel(out, 0.5, 0.5).name().toStdString();
    EXPECT_TRUE(test::near(pixel(out, 0.1, 0.5), QColor(220, 30, 30), 8));
}

TEST(Compositor, TextAndSubtitlesAreDrawnWhereTheyBelong) {
    test::EditorFixture f({.camera = false});
    f.addText("Lesson 1", 0.0, 3.0, "callout");
    timeline::addSubtitleCues(f.project.timeline, {{TimeRange::fromStartEnd(sec(0), sec(3)), "Welcome to the course"}});
    Render r(f);
    const QImage& img = r.at(f.project, 1.0);
    // The callout's yellow box around its anchor at the top center.
    int yellow = 0;
    const int cy = static_cast<int>(textPresetDefaults("callout").y * 720);
    for (int y = cy - 30; y < cy + 30; ++y) {
        for (int x = 440; x < 840; ++x) {
            const QColor c = img.pixelColor(x, y);
            yellow += (c.red() > 200 && c.green() > 150 && c.blue() < 120) ? 1 : 0;
        }
    }
    EXPECT_GT(yellow, 2000);
    // Subtitle: dark box with white text around its band.
    int whiteish = 0;
    const int y0 = static_cast<int>(f.project.style.subtitlePosition * 720) - 15;
    for (int y = y0; y < y0 + 30; ++y) {
        for (int x = 400; x < 880; ++x) whiteish += img.pixelColor(x, y).lightness() > 200 ? 1 : 0;
    }
    EXPECT_GT(whiteish, 50);
}

TEST(Compositor, LongSubtitlesWrapOntoSeveralLines) {
    RenderPlan plan;
    plan.width = 640;
    plan.height = 360;
    plan.background = "#000000";
    VisualLayer sub;
    sub.kind = LayerKind::Subtitle;
    sub.text = "Every edit you see here is rendered by the same compositor as the export, frame for frame.";
    sub.anchorX = 0.5;
    sub.anchorY = 0.7;
    plan.layers.push_back(sub);
    QImage img(640, 360, QImage::Format_RGB32);
    Compositor().render(plan, img, {});
    // Rows that contain white glyph pixels form separate text lines.
    int lines = 0;
    bool inLine = false;
    int lowest = 0;
    for (int y = 0; y < 360; ++y) {
        bool white = false;
        for (int x = 0; x < 640 && !white; ++x) white = img.pixelColor(x, y).lightness() > 200;
        if (white && !inLine) ++lines;
        if (white) lowest = y;
        inLine = white;
    }
    EXPECT_GE(lines, 2);
    EXPECT_LT(lowest, 359);  // nothing clipped at the canvas edge
    // Every glyph stays inside the canvas width (84 % band).
    for (int y = 0; y < 360; ++y) {
        EXPECT_LT(img.pixelColor(2, y).lightness(), 100);
        EXPECT_LT(img.pixelColor(637, y).lightness(), 100);
    }
}

TEST(Compositor, SubtitlesNeverLoseWordsAtAnyWidth) {
    // Regression: when a sentence measured almost exactly the wrap width, the
    // box was sized for one line while painting wrapped the last word onto a
    // second, clipped line. Every glyph must be drawn at every canvas width.
    const std::string text = "Every edit you see here is rendered by the same compositor as the export.";
    auto ink = [&](int width) {
        RenderPlan plan;
        plan.width = width;
        plan.height = 440;
        plan.background = "#000000";
        plan.style.subtitleBackground = "";  // glyphs only
        VisualLayer sub;
        sub.kind = LayerKind::Subtitle;
        sub.text = text;
        sub.anchorX = 0.5;
        sub.anchorY = 0.6;
        plan.layers.push_back(sub);
        QImage img(width, 440, QImage::Format_RGB32);
        Compositor().render(plan, img, {});
        long count = 0;
        for (int y = 0; y < img.height(); ++y) {
            const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) count += qGreen(row[x]) > 128 ? 1 : 0;
        }
        return count;
    };
    const long reference = ink(4000);  // one line, nothing can clip
    ASSERT_GT(reference, 500);
    // Every width: the failure window is about a pixel wide.
    for (int width = 600; width <= 900; ++width) {
        ASSERT_GT(ink(width), reference * 97 / 100) << "canvas width " << width;
    }
}

TEST(Compositor, SubtitlesStepAboveATitleInTheirWay) {
    RenderPlan plan;
    plan.width = 1280;
    plan.height = 720;
    plan.background = "#000000";
    plan.style.subtitleBackground = "";
    VisualLayer title;
    title.kind = LayerKind::Text;
    title.text = "Speaker name";
    title.textStyle.size = 52;
    title.textStyle.color = "#FFFFFF00";          // invisible glyphs…
    title.textStyle.background = "#FF2040F0";     // …on a solid blue box
    title.anchorX = 0.5;
    title.anchorY = 0.88;
    plan.layers.push_back(title);
    VisualLayer sub;
    sub.kind = LayerKind::Subtitle;
    sub.text = "What the speaker says";
    sub.anchorX = 0.5;
    sub.anchorY = 0.88;  // same band as the title
    plan.layers.push_back(sub);
    QImage img(1280, 720, QImage::Format_RGB32);
    Compositor().render(plan, img, {});
    int boxTop = 720;
    int lowestWhite = -1;
    for (int y = 0; y < 720; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < 1280; ++x) {
            const QRgb c = row[x];
            if (qBlue(c) > 200 && qRed(c) < 80) boxTop = std::min(boxTop, y);
            if (qRed(c) > 200 && qGreen(c) > 200 && qBlue(c) > 200) lowestWhite = std::max(lowestWhite, y);
        }
    }
    ASSERT_LT(boxTop, 720);
    ASSERT_GE(lowestWhite, 0);
    EXPECT_LT(lowestWhite, boxTop);  // the subtitle sits entirely above the title box
}

TEST(Compositor, MissingMediaDrawsAPlaceholder) {
    test::EditorFixture f({.camera = false});
    std::filesystem::remove(f.dir / "screen.mkv");
    Render r(f);
    const QImage& img = r.at(f.project, 1.0);
    EXPECT_TRUE(test::near(pixel(img, 0.5, 0.5), QColor(30, 33, 40), 4));
}

TEST(ColorAndBlur, GradeAndSoftenImages) {
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(QColor(200, 100, 50));
    applyColor(img, {.saturation = -1.0});
    const QColor gray = img.pixelColor(10, 10);
    EXPECT_EQ(gray.red(), gray.green());
    EXPECT_EQ(gray.green(), gray.blue());
    img.fill(QColor(60, 60, 60));
    applyColor(img, {.exposure = 1.0});
    EXPECT_NEAR(img.pixelColor(5, 5).red(), 120, 2);
    img.fill(QColor(128, 128, 128));
    applyColor(img, {.temperature = 1.0});
    EXPECT_GT(img.pixelColor(5, 5).red(), img.pixelColor(5, 5).blue());

    QImage edge(200, 40, QImage::Format_RGB32);
    edge.fill(Qt::black);
    QPainter p(&edge);
    p.fillRect(100, 0, 100, 40, Qt::white);
    p.end();
    blurImage(edge, 8);
    EXPECT_GT(edge.pixelColor(100, 20).red(), 60);
    EXPECT_LT(edge.pixelColor(100, 20).red(), 200);
    EXPECT_LT(edge.pixelColor(10, 20).red(), 5);
    EXPECT_GT(edge.pixelColor(190, 20).red(), 250);
}
