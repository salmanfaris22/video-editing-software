// Direct manipulation on the canvas: hand-placed layouts per canvas aspect,
// the visible bounds of every layer (the outline drawn on the preview), and
// picking the layer under the pointer. Plus text animations.

#include "editor/Compositor.h"
#include "editor/EditorFixture.h"
#include "editor/FrameProvider.h"
#include "editor/LayerGeometry.h"
#include "timeline/EditOps.h"
#include "timeline/Subtitles.h"

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::editor;

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

int indexOf(const RenderPlan& plan, const std::string& role) {
    for (std::size_t i = 0; i < plan.layers.size(); ++i) {
        if (plan.layers[i].role == role) return static_cast<int>(i);
    }
    return -1;
}

const VisualLayer& layer(const RenderPlan& plan, const std::string& role) {
    const int i = indexOf(plan, role);
    EXPECT_GE(i, 0) << role;
    return plan.layers[static_cast<std::size_t>(std::max(0, i))];
}

void expectRect(const NormRect& a, const NormRect& b, double tolerance = 1e-9) {
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.w, b.w, tolerance);
    EXPECT_NEAR(a.h, b.h, tolerance);
}

/// Bounding box (pixels) of the pixels matching `pred`; empty when none.
QRect boundsOf(const QImage& img, const std::function<bool(QColor)>& pred) {
    int x0 = img.width();
    int y0 = img.height();
    int x1 = -1;
    int y1 = -1;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (!pred(img.pixelColor(x, y))) continue;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    }
    return x1 < 0 ? QRect() : QRect(QPoint(x0, y0), QPoint(x1, y1));
}

QImage renderPlan(const RenderPlan& plan, const test::EditorFixture& f, int width, int height) {
    FrameProvider frames(f.dir.path(), false);
    frames.setProject(f.snapshot());
    Compositor compositor;
    QImage canvas(width, height, QImage::Format_RGB32);
    compositor.render(plan, canvas, [&frames](const VisualLayer& l, QSizeF box) { return frames.image(l, box); });
    return canvas;
}

bool isYellow(QColor c) { return c.red() > 200 && c.green() > 150 && c.blue() < 120; }

}  // namespace

TEST(CanvasLayout, HandPlacedSlotsApplyPerPresetAndCanvasAspect) {
    test::EditorFixture f;  // 1280×720, camera bottom-right
    const project::SlotRect custom{0.05, 0.08, 0.30, 0.30};
    f.project.style.layouts[project::layoutKey("pip.bottom-right.rounded", 1280, 720)].camera = custom;
    EXPECT_EQ(project::layoutKey("pip.bottom-right.rounded", 1920, 1080), "pip.bottom-right.rounded@16:9");

    const RenderPlan plan = buildRenderPlan(f.project, sec(1));
    EXPECT_EQ(plan.layout, "pip.bottom-right.rounded");
    expectRect(layer(plan, "camera").box, {0.05, 0.08, 0.30, 0.30});
    EXPECT_GT(layer(plan, "camera").radius, 0);  // still styled as a camera bubble
    // The screen keeps the preset's slot.
    expectRect(layer(plan, "screen").box, {0, 0, 1, 1});

    // Another canvas aspect has its own arrangement: the preset again.
    f.project.canvas.width = 1080;
    f.project.canvas.height = 1920;
    const RenderPlan portrait = buildRenderPlan(f.project, sec(1));
    expectRect(layer(portrait, "camera").box, *layoutSlots("pip.bottom-right.rounded", 1080, 1920).camera);

    // Other presets are not affected.
    f.project.canvas.width = 1280;
    f.project.canvas.height = 720;
    timeline::edit::setLayoutAll(f.project.timeline, "pip.bottom-left.rounded");
    expectRect(layer(buildRenderPlan(f.project, sec(1)), "camera").box,
               *layoutSlots("pip.bottom-left.rounded", 1280, 720).camera);
}

TEST(CanvasLayout, ACustomizedFullScreenCameraBecomesABubble) {
    test::EditorFixture f;
    timeline::edit::setLayoutAll(f.project.timeline, "camera.only");
    EXPECT_EQ(layer(buildRenderPlan(f.project, sec(1)), "camera").radius, 0);
    f.project.style.layouts[project::layoutKey("camera.only", 1280, 720)].camera = project::SlotRect{0.6, 0.6, 0.3, 0.3};
    const RenderPlan plan = buildRenderPlan(f.project, sec(1));
    const VisualLayer& cam = layer(plan, "camera");
    expectRect(cam.box, {0.6, 0.6, 0.3, 0.3});
    EXPECT_GT(cam.radius, 0);
}

TEST(LayerGeometry, TheScreenOutlineIsThePictureNotItsSlot) {
    test::EditorFixture f({.camera = false});  // 640×360 screen
    f.project.canvas.width = 1080;
    f.project.canvas.height = 1920;  // 9:16: the 16:9 screen is a strip in the middle
    const RenderPlan plan = buildRenderPlan(f.project, sec(1));
    const int screen = indexOf(plan, "screen");
    ASSERT_GE(screen, 0);
    EXPECT_NEAR(plan.layers[static_cast<std::size_t>(screen)].sourceAspect, 16.0 / 9.0, 1e-9);
    const NormRect shown = layerBounds(plan)[static_cast<std::size_t>(screen)].rect;
    const double h = 1080.0 / (16.0 / 9.0) / 1920.0;
    expectRect(shown, {0, 0.5 - h / 2, 1, h});

    // Above and below the strip there is nothing to pick.
    EXPECT_EQ(layerAt(plan, 0.5, 0.5), screen);
    EXPECT_EQ(layerAt(plan, 0.5, 0.2), -1);
    EXPECT_EQ(layerAt(plan, 0.5, 0.8), -1);

    // The same geometry is what the compositor draws.
    const QImage img = renderPlan(plan, f, 270, 480);
    const QRect drawn = boundsOf(img, [](QColor c) { return c.blue() > 60; });  // the bluish screen
    EXPECT_NEAR(drawn.top(), shown.y * 480, 1.5);
    EXPECT_NEAR(drawn.bottom() + 1, (shown.y + shown.h) * 480, 1.5);
}

TEST(LayerGeometry, PicksTheTopmostLayerAndRespectsCircles) {
    test::EditorFixture f;
    f.addText("Hello", 0.0, 3.0, "callout");
    f.project.style.cameraShape = "circle";
    const RenderPlan plan = buildRenderPlan(f.project, sec(1.5));
    const auto bounds = layerBounds(plan);
    const int camera = indexOf(plan, "camera");
    const int text = indexOf(plan, "text");
    const int screen = indexOf(plan, "screen");
    const NormRect cam = bounds[static_cast<std::size_t>(camera)].rect;
    EXPECT_TRUE(bounds[static_cast<std::size_t>(camera)].circle);
    EXPECT_EQ(layerAt(plan, cam.x + cam.w / 2, cam.y + cam.h / 2), camera);
    // The circle's bounding-box corner shows the screen below.
    EXPECT_EQ(layerAt(plan, cam.x + cam.w * 0.03, cam.y + cam.h * 0.03), screen);
    const NormRect t = bounds[static_cast<std::size_t>(text)].rect;
    EXPECT_EQ(layerAt(plan, t.x + t.w / 2, t.y + t.h / 2), text);
    EXPECT_EQ(layerAt(plan, 0.3, 0.5), screen);
}

TEST(LayerGeometry, TextBoundsMatchTheDrawnBox) {
    test::EditorFixture f({.camera = false});
    f.track("Screen").hidden = true;  // plain background
    f.addText("Lesson one: shortcuts", 0.0, 3.0, "callout");
    const RenderPlan plan = buildRenderPlan(f.project, sec(1.5));
    const NormRect box = layerBounds(plan)[static_cast<std::size_t>(indexOf(plan, "text"))].rect;
    // The callout's yellow background is exactly its block; check at two sizes
    // (the outline is resolution independent).
    for (const QSize size : {QSize(1280, 720), QSize(640, 360)}) {
        const QImage img = renderPlan(plan, f, size.width(), size.height());
        const QRect yellow = boundsOf(img, isYellow);
        ASSERT_FALSE(yellow.isNull());
        EXPECT_NEAR(yellow.left(), box.x * size.width(), 2.0) << size.width();
        EXPECT_NEAR(yellow.right() + 1, (box.x + box.w) * size.width(), 2.0);
        EXPECT_NEAR(yellow.top(), box.y * size.height(), 2.0);
        EXPECT_NEAR(yellow.bottom() + 1, (box.y + box.h) * size.height(), 2.0);
    }
}

TEST(LayerGeometry, SubtitleBoundsStepAboveTitlesLikeTheirPixels) {
    test::EditorFixture f({.camera = false});
    f.addText("Chapter", 0.0, 3.0, "caption");  // sits on the subtitle band
    timeline::addSubtitleCues(f.project.timeline, {{TimeRange::fromStartEnd(sec(0), sec(3)), "Hello there"}});
    const RenderPlan plan = buildRenderPlan(f.project, sec(1.5));
    const auto bounds = layerBounds(plan);
    const NormRect title = bounds[static_cast<std::size_t>(indexOf(plan, "text"))].rect;
    const NormRect sub = bounds[static_cast<std::size_t>(indexOf(plan, "subtitle"))].rect;
    EXPECT_LE(sub.y + sub.h, title.y + 1e-9);  // moved above the caption
}

TEST(TextAnimation, EntrancesAndExitsAtEveryInstant) {
    test::EditorFixture f;
    const timeline::ClipId id = f.addText("Hello", 1.0, 2.0, "title");
    auto setAnimation = [&](const std::string& in, const std::string& out) {
        for (auto& t : f.project.timeline.tracks) {
            for (auto& c : t.clips) {
                if (c.id == id) c.text->animation = {in, out, sec(0.4), sec(0.4)};
            }
        }
    };
    auto text = [&](double t) { return layer(buildRenderPlan(f.project, sec(t)), "text"); };

    setAnimation("slide-up", "slide-up");
    EXPECT_NEAR(text(1.0).textDy, 0.06, 1e-9);  // starts below
    EXPECT_NEAR(text(1.0).opacity, 0.0, 1e-9);
    EXPECT_NEAR(text(1.2).textDy, 0.06 * 0.125, 1e-9);  // ease-out: most of the way at half time
    EXPECT_NEAR(text(1.2).opacity, 0.5, 1e-9);
    EXPECT_DOUBLE_EQ(text(2.0).textDy, 0.0);
    EXPECT_DOUBLE_EQ(text(2.0).opacity, 1.0);
    EXPECT_LT(text(2.8).textDy, 0.0);  // leaves upwards

    setAnimation("pop", "zoom");
    EXPECT_NEAR(text(1.0).textScale, 0.5, 1e-9);
    EXPECT_GT(text(1.3).textScale, 1.0);  // overshoot
    EXPECT_NEAR(text(1.5).textScale, 1.0, 1e-9);
    EXPECT_GT(text(2.9).textScale, 1.0);

    setAnimation("typewriter", "wipe");
    EXPECT_EQ(text(1.0).textChars, 0);
    EXPECT_EQ(text(1.25).textChars, 3);  // 5 letters × 0.625
    EXPECT_EQ(text(2.0).textChars, -1);
    EXPECT_DOUBLE_EQ(text(2.0).textReveal, 1.0);
    EXPECT_NEAR(text(2.9).textReveal, 1.0 - std::pow(0.75, 3.0), 1e-9);  // a quarter of the exit left

    setAnimation("none", "none");
    EXPECT_DOUBLE_EQ(text(1.0).opacity, 1.0);
    EXPECT_DOUBLE_EQ(text(2.99).opacity, 1.0);

    // A short clip: in and out share it, never overlap.
    setAnimation("fade", "fade");
    for (auto& t : f.project.timeline.tracks) {
        for (auto& c : t.clips) {
            if (c.id == id) c.range.duration = sec(0.4);
        }
    }
    EXPECT_NEAR(text(1.1).opacity, 0.5, 1e-9);
    EXPECT_NEAR(text(1.3).opacity, 0.5, 1e-9);
}

TEST(TextAnimation, WipeAndTypewriterDrawOnlyWhatIsRevealed) {
    test::EditorFixture f({.camera = false});
    f.track("Screen").hidden = true;
    const timeline::ClipId id = f.addText("Wipe me in", 0.0, 3.0, "callout");
    auto setAnimation = [&](const std::string& in) {
        for (auto& t : f.project.timeline.tracks) {
            for (auto& c : t.clips) {
                if (c.id == id) c.text->animation = {in, "none", sec(1.0), sec(0)};
            }
        }
    };
    setAnimation("none");
    const QRect full = boundsOf(renderPlan(buildRenderPlan(f.project, sec(2)), f, 640, 360), isYellow);
    setAnimation("wipe");
    const RenderPlan half = buildRenderPlan(f.project, sec(0.2063));  // ease-out(0.2063) ≈ 0.5
    EXPECT_NEAR(layer(half, "text").textReveal, 0.5, 0.01);
    const QRect wiped = boundsOf(renderPlan(half, f, 640, 360), isYellow);
    EXPECT_EQ(wiped.left(), full.left());
    EXPECT_NEAR(wiped.width(), full.width() * 0.5, 3.0);

    // Typewriter: the box is there, the letters come one by one.
    auto letters = [&](double t) {
        const QImage img = renderPlan(buildRenderPlan(f.project, sec(t)), f, 640, 360);
        int dark = 0;
        for (int y = full.top(); y <= full.bottom(); ++y) {
            for (int x = full.left(); x <= full.right(); ++x) dark += img.pixelColor(x, y).lightness() < 90 ? 1 : 0;
        }
        return dark;
    };
    setAnimation("typewriter");
    const int partial = letters(0.5);
    const int all = letters(2.0);
    EXPECT_GT(partial, 0);
    EXPECT_LT(partial, all * 0.8);
}
