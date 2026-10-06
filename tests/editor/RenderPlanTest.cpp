#include "editor/EditorFixture.h"
#include "editor/RenderPlan.h"
#include "timeline/EditOps.h"
#include "timeline/Subtitles.h"

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::editor;

namespace {
Time sec(double s) { return Time::fromSecondsF(s); }

const VisualLayer* layer(const RenderPlan& plan, const std::string& role) {
    for (const auto& l : plan.layers) {
        if (l.role == role) return &l;
    }
    return nullptr;
}
}  // namespace

TEST(RenderPlan, PictureInPictureComesFromTheLayoutPreset) {
    test::EditorFixture f;
    const RenderPlan plan = buildRenderPlan(f.project, sec(1));
    ASSERT_EQ(plan.layers.size(), 2u);
    EXPECT_EQ(plan.layers[0].role, "screen");
    EXPECT_EQ(plan.layers[1].role, "camera");  // camera above the screen
    const auto pip = layoutSlots("pip.bottom-right.rounded", 1280, 720);
    EXPECT_EQ(plan.layers[0].box, NormRect{});
    EXPECT_EQ(plan.layers[1].box, *pip.camera);
    EXPECT_TRUE(plan.layers[1].fill);
    EXPECT_GT(plan.layers[1].radius, 0);
    EXPECT_GT(plan.layers[1].shadow, 0);
    EXPECT_FALSE(plan.layers[0].fill);
    EXPECT_NEAR(plan.layers[0].sourceTime.toSecondsF(), 1.0, 1e-9);
}

TEST(RenderPlan, LayoutSectionsAndMissingSourceFallbacks) {
    test::EditorFixture f;
    ASSERT_TRUE(timeline::edit::setLayoutFrom(f.project.timeline, sec(1.5), "camera.only"));
    EXPECT_EQ(buildRenderPlan(f.project, sec(1)).layers.size(), 2u);
    const RenderPlan cam = buildRenderPlan(f.project, sec(2));
    ASSERT_EQ(cam.layers.size(), 1u);
    EXPECT_EQ(cam.layers[0].role, "camera");
    EXPECT_EQ(cam.layers[0].box, NormRect{});
    EXPECT_EQ(cam.layers[0].radius, 0);  // full bleed
    timeline::edit::setLayoutAll(f.project.timeline, "screen.only");
    ASSERT_EQ(buildRenderPlan(f.project, sec(2)).layers.size(), 1u);

    test::EditorFixture screenOnly({.camera = false});
    timeline::edit::setLayoutAll(screenOnly.project.timeline, "camera.only");  // no camera recorded
    const RenderPlan fallback = buildRenderPlan(screenOnly.project, sec(1));
    ASSERT_EQ(fallback.layers.size(), 1u);
    EXPECT_EQ(fallback.layers[0].role, "screen");
}

TEST(RenderPlan, StyleTransformAndVisibility) {
    test::EditorFixture f;
    f.project.style.screenPadding = 0.05;
    f.project.style.screenRadius = 0.02;
    f.project.style.cameraShape = "circle";
    RenderPlan plan = buildRenderPlan(f.project, sec(1));
    const VisualLayer* screen = layer(plan, "screen");
    ASSERT_TRUE(screen);
    EXPECT_NEAR(screen->box.y, 0.05, 1e-9);
    EXPECT_NEAR(screen->box.x, 0.05 * 720 / 1280, 1e-9);
    EXPECT_DOUBLE_EQ(screen->radius, 0.02);
    const VisualLayer* camera = layer(plan, "camera");
    ASSERT_TRUE(camera);
    EXPECT_TRUE(camera->circle);
    EXPECT_NEAR(camera->box.w * 1280, camera->box.h * 720, 1e-6);  // square in pixels

    // The clip transform adjusts the slot: 1.5× about its center, then 0.1 to the left.
    f.project.style.cameraShape = "rounded";
    const RenderPlan beforePlan = buildRenderPlan(f.project, sec(1));
    ASSERT_TRUE(layer(beforePlan, "camera"));
    const NormRect before = layer(beforePlan, "camera")->box;
    auto& camClip = f.track("Camera").clips[0];
    camClip.transform.scale = timeline::Vec2{1.5, 1.5};
    camClip.transform.position = timeline::Vec2{0.4, 0.5};
    plan = buildRenderPlan(f.project, sec(1));
    const NormRect after = layer(plan, "camera")->box;
    EXPECT_NEAR(after.w, before.w * 1.5, 1e-9);
    EXPECT_NEAR(after.x + after.w / 2, before.x + before.w / 2 - 0.1, 1e-9);
    EXPECT_NEAR(after.y + after.h / 2, before.y + before.h / 2, 1e-9);

    f.track("Camera").hidden = true;
    f.track("Screen").clips[0].enabled = false;
    EXPECT_TRUE(buildRenderPlan(f.project, sec(1)).layers.empty());
}

TEST(RenderPlan, TextSubtitlesEffectsAndColor) {
    test::EditorFixture f;
    f.addText("Chapter one", 0.5, 2.0, "lower-third");
    timeline::addSubtitleCues(f.project.timeline, {{TimeRange::fromStartEnd(sec(0.8), sec(1.8)), "Hello there"}});
    auto& screenClip = f.track("Screen").clips[0];
    timeline::EffectInstance zoom{timeline::EffectId::generate(), kEffectZoom, 1, true, {}};
    zoom.params["scale"] = 2.0;
    zoom.params["x"] = 0.25;
    zoom.params["y"] = 0.75;
    screenClip.effects.push_back(zoom);
    timeline::EffectInstance blur{timeline::EffectId::generate(), kEffectBlur, 1, true, {}};
    blur.params["amount"] = 0.4;
    screenClip.effects.push_back(blur);
    screenClip.color.saturation = -0.5;

    const RenderPlan plan = buildRenderPlan(f.project, sec(1));
    const VisualLayer* text = layer(plan, "text");
    ASSERT_TRUE(text);
    EXPECT_EQ(text->text, "Chapter one");
    EXPECT_DOUBLE_EQ(text->anchorX, textPresetDefaults("lower-third").x);
    EXPECT_DOUBLE_EQ(text->opacity, 1.0);
    const VisualLayer* sub = layer(plan, "subtitle");
    ASSERT_TRUE(sub);
    EXPECT_EQ(sub->text, "Hello there");
    EXPECT_DOUBLE_EQ(sub->anchorY, f.project.style.subtitlePosition);
    EXPECT_EQ(plan.layers.back().role, "subtitle");  // subtitles on top
    const VisualLayer* screen = layer(plan, "screen");
    EXPECT_DOUBLE_EQ(screen->zoom, 2.0);
    EXPECT_DOUBLE_EQ(screen->zoomX, 0.25);
    EXPECT_DOUBLE_EQ(screen->blur, 0.4);
    EXPECT_DOUBLE_EQ(screen->color.saturation, -0.5);

    // Titles fade in over 150 ms.
    const RenderPlan fadePlan = buildRenderPlan(f.project, sec(0.55));
    const VisualLayer* fading = layer(fadePlan, "text");
    ASSERT_TRUE(fading);
    EXPECT_NEAR(fading->opacity, 0.05 / 0.15, 0.01);
    EXPECT_FALSE(layer(buildRenderPlan(f.project, sec(2.6)), "text"));
}

TEST(RenderPlan, OverlaysArePlacedFreely) {
    test::EditorFixture f;
    project::MediaSource logo;
    logo.id = project::MediaId::generate();
    logo.kind = project::MediaKind::Image;
    logo.name = "Logo";
    logo.path = "logo.png";
    logo.info.video = project::VideoMetadata{"png", 400, 200, FrameRate(30, 1), "rgba", "", "", 0, false};
    f.project.media.push_back(logo);
    timeline::Track overlays{timeline::TrackId::generate(), timeline::TrackKind::Overlay, "Overlays"};
    timeline::Clip c;
    c.id = timeline::ClipId::generate();
    c.media = logo.id;
    c.range = {sec(0), sec(3)};
    c.transform.position = timeline::Vec2{0.8, 0.2};
    c.transform.scale = timeline::Vec2{0.25, 0.25};
    ASSERT_TRUE(overlays.insertClip(c));
    f.project.timeline.tracks.push_back(overlays);
    const RenderPlan overlayPlan = buildRenderPlan(f.project, sec(1));
    const VisualLayer* l = layer(overlayPlan, "overlay");
    ASSERT_TRUE(l);
    EXPECT_NEAR(l->box.w, 0.25, 1e-9);
    EXPECT_NEAR(l->box.h, 0.25 * 1280 / 2.0 / 720, 1e-9);
    EXPECT_NEAR(l->box.x, 0.8 - 0.125, 1e-9);
    EXPECT_EQ(l->mediaKind, project::MediaKind::Image);
}

TEST(LayoutPresets, EveryPresetIsWellFormedOnEveryCanvas) {
    for (const auto& [w, h] : {std::pair{1920, 1080}, {1080, 1920}, {1080, 1080}, {1080, 1350}}) {
        for (const auto& preset : layoutPresets()) {
            const LayoutSlots ls = layoutSlots(preset.id, w, h);
            EXPECT_TRUE(ls.screen || ls.camera) << preset.id;
            for (const auto& slot : {ls.screen, ls.camera}) {
                if (!slot) continue;
                EXPECT_GE(slot->x, -1e-9) << preset.id << " " << w << "x" << h;
                EXPECT_GE(slot->y, -1e-9) << preset.id;
                EXPECT_LE(slot->x + slot->w, 1 + 1e-9) << preset.id;
                EXPECT_LE(slot->y + slot->h, 1 + 1e-9) << preset.id;
                EXPECT_GT(slot->w * slot->h, 0.01) << preset.id;
            }
        }
    }
    EXPECT_TRUE(isKnownLayout("pip.bottom-right.circle"));
    EXPECT_FALSE(isKnownLayout("nope"));
}
