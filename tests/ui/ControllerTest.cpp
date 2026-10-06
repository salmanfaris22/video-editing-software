#include "editor/EditorFixture.h"
#include "editor/RenderPlan.h"
#include "media/MediaProbe.h"
#include "project/ProjectStore.h"
#include "editor/ColorGrading.h"
#include "ui/ExportController.h"
#include "ui/FrameGrab.h"
#include "ui/LookPreviews.h"
#include "ui/PlaybackController.h"
#include "ui/ProjectController.h"

#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QUrl>

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <functional>
#include <thread>

using namespace lectern;
using namespace lectern::ui;

namespace {

bool waitUntil(const std::function<bool()>& pred, int timeoutMs = 10'000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

/// A saved recording-like project opened in a ProjectController.
struct OpenProject {
    test::EditorFixture fixture;
    ProjectController controller;
    explicit OpenProject(const test::EditorFixture::Options& options = {}) : fixture(options) {
        EXPECT_TRUE(project::ProjectStore::save(fixture.dir.path(), fixture.project));
        controller.open(QString::fromStdString(fixture.dir.path().string()));
        EXPECT_TRUE(waitUntil([this] { return controller.loaded(); }));
    }
    QVariantMap track(const QString& name) const {
        for (const QVariant& t : controller.tracks()) {
            if (t.toMap().value("name").toString() == name) return t.toMap();
        }
        return {};
    }
    QVariantList clips(const QString& name) const { return track(name).value("clips").toList(); }
    QString clipId(const QString& trackName, int index = 0) const {
        return clips(trackName).value(index).toMap().value("id").toString();
    }
    /// The project as saved on disk (after the debounced save ran).
    project::Project saved() {
        controller.flushSaves();
        auto loaded = project::ProjectStore::load(fixture.dir.path());
        EXPECT_TRUE(loaded);
        return loaded ? loaded->project : project::Project{};
    }
};

}  // namespace

TEST(ProjectControllerUi, SplitDeleteUndoRedoAndSave) {
    OpenProject p;
    p.controller.setLinkedEditMode(QStringLiteral("allTracks"));  // a recording segment deletes on every track
    EXPECT_NEAR(p.controller.duration(), 3.0, 1e-6);
    EXPECT_EQ(p.controller.tracks().size(), 3);  // screen, camera, microphone

    p.controller.splitAt(1.0);
    EXPECT_EQ(p.clips("Screen").size(), 2);
    EXPECT_EQ(p.clips("Microphone").size(), 2);
    EXPECT_TRUE(p.controller.canUndo());

    p.controller.selectClip(p.clipId("Camera", 1));
    EXPECT_EQ(p.controller.selection().value("role").toString(), "camera");
    EXPECT_TRUE(p.controller.selection().value("linked").toBool());
    p.controller.deleteSelected();
    EXPECT_NEAR(p.controller.duration(), 1.0, 1e-6);  // the right segment went from every track
    EXPECT_TRUE(p.controller.selectedClip().isEmpty());

    p.controller.undo();
    EXPECT_NEAR(p.controller.duration(), 3.0, 1e-6);
    p.controller.undo();
    EXPECT_EQ(p.clips("Screen").size(), 1);
    EXPECT_FALSE(p.controller.canUndo());
    p.controller.redo();
    EXPECT_EQ(p.clips("Screen").size(), 2);

    // The snapshot follows edits; the saved file matches the document.
    ASSERT_TRUE(p.controller.snapshot());
    EXPECT_EQ(p.controller.snapshot()->timeline.tracks.front().clips.size(), 2u);
    const project::Project onDisk = p.saved();
    EXPECT_EQ(onDisk.timeline, p.controller.snapshot()->timeline);
}

TEST(ProjectControllerUi, SliderDragsMergeIntoOneUndoStep) {
    OpenProject p;
    for (int i = 1; i <= 20; ++i) p.controller.setStyleValue("screenPadding", i * 0.005);
    EXPECT_DOUBLE_EQ(p.controller.style().value("screenPadding").toDouble(), 0.1);
    p.controller.setStyleValue("cameraShape", "circle");
    p.controller.undo();  // shape
    p.controller.undo();  // the whole padding drag
    EXPECT_DOUBLE_EQ(p.controller.style().value("screenPadding").toDouble(), 0.0);
    EXPECT_FALSE(p.controller.canUndo());
}

TEST(ProjectControllerUi, TextStyleEffectsColorAndLayout) {
    OpenProject p;
    p.controller.addText("Welcome", 0.5, 2.0, "lower-third");
    ASSERT_EQ(p.controller.selection().value("role").toString(), "text");
    const QString text = p.controller.selectedClip();
    p.controller.setText(text, "Welcome back");
    p.controller.setTextValue(text, "size", 80);
    p.controller.setTextValue(text, "color", "#FFF5C142");
    EXPECT_EQ(p.controller.selection().value("text").toString(), "Welcome back");
    EXPECT_DOUBLE_EQ(p.controller.selection().value("textSize").toDouble(), 80.0);
    EXPECT_EQ(p.controller.selection().value("textColor").toString(), "#FFF5C142");
    p.controller.setTextValue(text, "preset", "callout");  // preset resets the look and placement
    EXPECT_EQ(p.controller.selection().value("preset").toString(), "callout");
    EXPECT_NE(p.controller.selection().value("textSize").toDouble(), 80.0);

    const QString screen = p.clipId("Screen");
    p.controller.selectClip(screen);
    p.controller.setEffectEnabled(screen, "zoom", true);
    p.controller.setEffectValue(screen, "zoom", "scale", 2.5);
    p.controller.setEffectEnabled(screen, "blur", true);
    EXPECT_TRUE(p.controller.selection().value("zoomOn").toBool());
    EXPECT_DOUBLE_EQ(p.controller.selection().value("zoom").toDouble(), 2.5);
    EXPECT_TRUE(p.controller.selection().value("blurOn").toBool());
    p.controller.setEffectEnabled(screen, "blur", false);
    EXPECT_FALSE(p.controller.selection().value("blurOn").toBool());

    p.controller.setColorValues(screen, {{"saturation", -1.0}, {"contrast", 0.1}});
    EXPECT_DOUBLE_EQ(p.controller.selection().value("saturation").toDouble(), -1.0);
    p.controller.resetColor(screen);
    EXPECT_DOUBLE_EQ(p.controller.selection().value("saturation").toDouble(), 0.0);

    p.controller.setLayoutFrom("camera.only", 1.5);
    EXPECT_EQ(p.controller.layoutAt(1.0), "pip.bottom-right.rounded");
    EXPECT_EQ(p.controller.layoutAt(2.0), "camera.only");
    EXPECT_EQ(p.controller.layoutRegions().size(), 2);
    const QVariantMap box = p.controller.layerBox(p.clipId("Camera"), 2.0);
    EXPECT_DOUBLE_EQ(box.value("w").toDouble(), 1.0);  // full canvas in camera.only

    // Everything lands in the saved project.
    const project::Project onDisk = p.saved();
    EXPECT_EQ(onDisk.timeline.layout.size(), 2u);
    bool foundText = false;
    for (const auto& t : onDisk.timeline.tracks) {
        for (const auto& c : t.clips) foundText = foundText || (c.text && c.text->text == "Welcome back");
    }
    EXPECT_TRUE(foundText);
}

namespace {

void expectBox(const QVariantMap& box, double x, double y, double w, double h, double tolerance = 1e-9) {
    ASSERT_FALSE(box.isEmpty());
    EXPECT_NEAR(box.value("x").toDouble(), x, tolerance);
    EXPECT_NEAR(box.value("y").toDouble(), y, tolerance);
    EXPECT_NEAR(box.value("w").toDouble(), w, tolerance);
    EXPECT_NEAR(box.value("h").toDouble(), h, tolerance);
}

double center(const QVariantMap& box, const char* axis) {
    const QString a = QString::fromLatin1(axis);
    return box.value(a).toDouble() + box.value(a == "x" ? "w" : "h").toDouble() / 2;
}

}  // namespace

TEST(ProjectControllerUi, PicksMovesAndResizesLayersOnTheCanvas) {
    OpenProject p;  // 1280×720, camera bottom-right
    const QString screen = p.clipId("Screen");
    const QString camera = p.clipId("Camera");
    const QVariantMap cam = p.controller.layerBox(camera, 1.0);
    EXPECT_EQ(cam.value("role").toString(), "camera");
    EXPECT_TRUE(cam.value("edges").toBool());
    EXPECT_EQ(p.controller.layerAt(center(cam, "x"), center(cam, "y"), 1.0), camera);
    EXPECT_EQ(p.controller.layerAt(0.2, 0.2, 1.0), screen);

    // A drag is many small moves: the camera follows exactly, as one undo step.
    const int before = p.controller.canUndo() ? 1 : 0;
    for (int i = 0; i <= 10; ++i) p.controller.setLayerRect(camera, 1.0, 0.05 + 0.01 * i, 0.1, 0.3, 0.3);
    expectBox(p.controller.layerBox(camera, 1.0), 0.15, 0.1, 0.3, 0.3);
    expectBox(p.controller.layerBox(camera, 2.5), 0.15, 0.1, 0.3, 0.3);  // the whole layout, not one moment
    EXPECT_EQ(before, 0);
    p.controller.undo();
    EXPECT_FALSE(p.controller.canUndo());
    expectBox(p.controller.layerBox(camera, 1.0), cam.value("x").toDouble(), cam.value("y").toDouble(),
              cam.value("w").toDouble(), cam.value("h").toDouble());
    p.controller.redo();
    EXPECT_TRUE(p.controller.layoutCustomized(1.0));

    // While dragging only the preview follows (no undo step, no view rebuild).
    int projectSignals = 0;
    QObject::connect(&p.controller, &ProjectController::projectChanged, [&] { ++projectSignals; });
    p.controller.previewLayerRect(camera, 1.0, 0.4, 0.45, 0.2, 0.2);
    EXPECT_EQ(projectSignals, 0);
    const editor::RenderPlan preview = editor::buildRenderPlan(*p.controller.snapshot(), Time::fromSeconds(1));
    for (const auto& l : preview.layers) {
        if (l.role == "camera") EXPECT_NEAR(l.box.x, 0.4, 1e-9);
    }
    expectBox(p.controller.layerBox(camera, 1.0), 0.15, 0.1, 0.3, 0.3);  // the document did not change
    p.controller.cancelPreview();
    {
        // Saving stamps modifiedAt with the current second; everything else must match.
        project::Project shown = *p.controller.snapshot();
        const project::Project onDisk = p.saved();
        shown.modifiedAt = onDisk.modifiedAt;
        EXPECT_EQ(shown, onDisk);
    }

    // On a 9:16 canvas the screen is a thin strip; make it fill the height and
    // show its left part. Padding is taken into account.
    p.controller.setStyleValue("screenPadding", 0.03);
    p.controller.setCanvasAspect("9:16");
    EXPECT_FALSE(p.controller.layoutCustomized(1.0));  // a new aspect starts from the preset
    const QVariantMap strip = p.controller.layerBox(screen, 1.0);
    EXPECT_LT(strip.value("h").toDouble(), 0.35);
    const double aspect = strip.value("aspect").toDouble();
    EXPECT_NEAR(aspect, 16.0 / 9.0, 1e-6);
    const double w = 2.4;
    const double h = w * p.controller.canvasWidth() / aspect / p.controller.canvasHeight();
    p.controller.setLayerRect(screen, 1.0, -0.2, 0.5 - h / 2, w, h);
    expectBox(p.controller.layerBox(screen, 1.0), -0.2, 0.5 - h / 2, w, h, 1e-6);
    // Back to 16:9: that arrangement is still there, untouched.
    p.controller.setCanvasAspect("16:9");
    expectBox(p.controller.layerBox(camera, 1.0), 0.15, 0.1, 0.3, 0.3);
    p.controller.resetLayoutCustomization(1.0);
    EXPECT_FALSE(p.controller.layoutCustomized(1.0));

    // Text: moving keeps it centered where dropped; resizing scales the font.
    p.controller.addText("Hello", 0.0, 3.0, "callout");
    const QString text = p.controller.selectedClip();
    p.controller.setTextValue(text, "animationIn", "none");
    const QVariantMap t0 = p.controller.layerBox(text, 1.0);
    const double size0 = p.controller.selection().value("textSize").toDouble();
    p.controller.setLayerRect(text, 1.0, 0.1, 0.4, t0.value("w").toDouble() * 2, t0.value("h").toDouble() * 2);
    EXPECT_NEAR(p.controller.selection().value("textSize").toDouble(), size0 * 2, 1e-9);
    const QVariantMap t1 = p.controller.layerBox(text, 1.0);
    EXPECT_NEAR(center(t1, "x"), 0.1 + t0.value("w").toDouble(), 2e-3);
    EXPECT_NEAR(center(t1, "y"), 0.4 + t0.value("h").toDouble(), 2e-3);
    EXPECT_NEAR(t1.value("h").toDouble(), t0.value("h").toDouble() * 2, 3e-3);
    EXPECT_EQ(p.controller.layerAt(center(t1, "x"), center(t1, "y"), 1.0), text);
    p.controller.resetLayerRect(text, 1.0);
    EXPECT_DOUBLE_EQ(p.controller.selection().value("textSize").toDouble(), size0);

    // Subtitles move vertically and change size.
    p.controller.addSubtitle("Caption line", 0.0, 3.0);
    QString subtitle;
    for (const QVariant& v : p.controller.subtitles()) subtitle = v.toMap().value("id").toString();
    const QVariantMap s0 = p.controller.layerBox(subtitle, 1.0);
    EXPECT_TRUE(s0.value("vertical").toBool());
    p.controller.setLayerRect(subtitle, 1.0, s0.value("x").toDouble(), 0.2, s0.value("w").toDouble(),
                              s0.value("h").toDouble() * 1.5);
    EXPECT_NEAR(p.controller.style().value("subtitlePosition").toDouble(), 0.2 + s0.value("h").toDouble() * 0.75, 1e-9);
    EXPECT_NEAR(p.controller.style().value("subtitleSize").toDouble(), 0.045 * 1.5, 1e-9);

    // All of it is saved.
    p.controller.setLayerRect(camera, 1.0, 0.5, 0.5, 0.25, 0.25);
    const project::Project onDisk = p.saved();
    const auto it = onDisk.style.layouts.find("pip.bottom-right.rounded@16:9");
    ASSERT_NE(it, onDisk.style.layouts.end());
    ASSERT_TRUE(it->second.camera.has_value());
    EXPECT_NEAR(it->second.camera->w, 0.25, 1e-9);
    EXPECT_TRUE(onDisk.style.layouts.contains("screen.only@9:16") || onDisk.style.layouts.contains("pip.bottom-right.rounded@9:16"));
}

TEST(ProjectControllerUi, TextAnimationsColorWheelsLutsAndBackgroundBlur) {
    OpenProject p;
    p.controller.addText("Title", 0.0, 2.0, "lower-third");
    const QString text = p.controller.selectedClip();
    EXPECT_EQ(p.controller.selection().value("animationIn").toString(), "slide-right");  // the preset's entrance
    p.controller.setTextValue(text, "animationIn", "typewriter");
    p.controller.setTextValue(text, "inDuration", 1.2);
    p.controller.setTextValue(text, "animationOut", "bogus");  // not an animation: refused
    EXPECT_EQ(p.controller.selection().value("animationIn").toString(), "typewriter");
    EXPECT_DOUBLE_EQ(p.controller.selection().value("inDuration").toDouble(), 1.2);
    EXPECT_EQ(p.controller.selection().value("animationOut").toString(), "fade");
    EXPECT_GE(p.controller.textAnimations().size(), 10);

    const QString camera = p.clipId("Camera");
    p.controller.selectClip(camera);
    p.controller.setColorWheel(camera, "lift", 3.0, 4.0, 0.2);  // outside the wheel: kept on its rim
    const QVariantMap lift = p.controller.selection().value("lift").toMap();
    EXPECT_NEAR(lift.value("x").toDouble(), 0.6, 1e-9);
    EXPECT_NEAR(lift.value("y").toDouble(), 0.8, 1e-9);
    EXPECT_NEAR(lift.value("master").toDouble(), 0.2, 1e-9);
    p.controller.setColorLut(camera, "builtin:s-log3");
    EXPECT_EQ(p.controller.selection().value("lutName").toString(), "Sony S-Log3 / S-Gamut3.Cine → Rec.709");
    p.controller.setColorValue(camera, "lutAmount", 0.4);
    EXPECT_DOUBLE_EQ(p.controller.selection().value("lutAmount").toDouble(), 0.4);
    // A look keeps the LUT and wheels it builds on.
    p.controller.setColorValues(camera, {{"saturation", 0.3}});
    EXPECT_EQ(p.controller.selection().value("lut").toString(), "builtin:s-log3");
    EXPECT_NEAR(p.controller.selection().value("lift").toMap().value("master").toDouble(), 0.2, 1e-9);

    // A .cube file is copied into the project and applied.
    const auto cubePath = p.fixture.dir / "Teal Orange.cube";
    {
        std::ofstream out(cubePath);
        out << "TITLE \"Teal\"\nLUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    }
    p.controller.importLut(camera, QUrl::fromLocalFile(QString::fromStdString(cubePath.string())));
    EXPECT_EQ(p.controller.selection().value("lut").toString(), "media/luts/Teal Orange.cube");
    EXPECT_EQ(p.controller.selection().value("lutName").toString(), "Teal Orange");
    EXPECT_TRUE(std::filesystem::exists(p.fixture.dir / "media" / "luts" / "Teal Orange.cube"));
    {
        std::ofstream bad(p.fixture.dir / "bad.cube");
        bad << "hello\n";
    }
    p.controller.importLut(camera, QUrl::fromLocalFile(QString::fromStdString((p.fixture.dir / "bad.cube").string())));
    EXPECT_TRUE(p.controller.message().contains("could not be read"));
    EXPECT_EQ(p.controller.selection().value("lut").toString(), "media/luts/Teal Orange.cube");

    p.controller.setEffectEnabled(camera, "background-blur", true);
    EXPECT_TRUE(p.controller.selection().value("backgroundBlurOn").toBool());
    EXPECT_DOUBLE_EQ(p.controller.selection().value("backgroundBlur").toDouble(), 0.6);
    p.controller.setEffectValue(camera, "background-blur", "amount", 0.9);
    EXPECT_DOUBLE_EQ(p.controller.selection().value("backgroundBlur").toDouble(), 0.9);

    const project::Project onDisk = p.saved();
    const timeline::Clip* cam = onDisk.timeline.findClip(*timeline::ClipId::parse(camera.toStdString()));
    ASSERT_TRUE(cam);
    EXPECT_EQ(cam->color.lut, "media/luts/Teal Orange.cube");
    EXPECT_NEAR(cam->color.lift.x, 0.6, 1e-9);
    bool animated = false;
    for (const auto& t : onDisk.timeline.tracks) {
        for (const auto& c : t.clips) {
            animated = animated || (c.text && c.text->animation.in == "typewriter" &&
                                    c.text->animation.inDuration == Time::fromSecondsF(1.2));
        }
    }
    EXPECT_TRUE(animated);
}

TEST(ProjectControllerUi, AudioMixerSettingsAndInvalidInputIsRejected) {
    OpenProject p;
    const QString mic = p.track("Microphone").value("id").toString();
    p.controller.setTrackValue(mic, "gainDb", -6.0);
    p.controller.setTrackValue(mic, "solo", true);
    EXPECT_DOUBLE_EQ(p.track("Microphone").value("gainDb").toDouble(), -6.0);
    EXPECT_TRUE(p.track("Microphone").value("solo").toBool());
    const QString micClip = p.clipId("Microphone");
    p.controller.setClipAudio(micClip, "fadeIn", 0.5);
    p.controller.setClipAudio(micClip, "fadeOut", 99);  // clamped to half the clip
    p.controller.selectClip(micClip);
    EXPECT_DOUBLE_EQ(p.controller.selection().value("fadeIn").toDouble(), 0.5);
    EXPECT_NEAR(p.controller.selection().value("fadeOut").toDouble(), 1.5, 1e-6);
    EXPECT_TRUE(p.controller.selection().value("hasAudio").toBool());

    p.controller.setStyleValue("nope", 1);
    EXPECT_FALSE(p.controller.message().isEmpty());
    p.controller.setCanvasAspect("7:3");  // ignored
    EXPECT_EQ(p.controller.aspect(), "16:9");
    p.controller.setCanvasAspect("9:16");
    EXPECT_EQ(p.controller.canvasWidth(), 1080);
    EXPECT_EQ(p.controller.canvasHeight(), 1920);
}

TEST(ProjectControllerUi, SubtitlesRoundTripThroughSrt) {
    OpenProject p;
    p.controller.addSubtitle("Hello", 0.2, 1.0);
    p.controller.addSubtitle("Second line", 1.5, 1.0);
    ASSERT_EQ(p.controller.subtitles().size(), 2);
    EXPECT_EQ(p.controller.selection().value("role").toString(), "subtitle");
    const auto srt = p.fixture.dir / "subs.srt";
    ASSERT_TRUE(p.controller.exportSubtitles(QUrl::fromLocalFile(QString::fromStdString(srt.string()))));
    p.controller.clearSubtitles();
    EXPECT_EQ(p.controller.subtitles().size(), 0);
    p.controller.importSubtitles(QUrl::fromLocalFile(QString::fromStdString(srt.string())));
    ASSERT_EQ(p.controller.subtitles().size(), 2);
    EXPECT_EQ(p.controller.subtitles()[1].toMap().value("text").toString(), "Second line");
}

TEST(ProjectControllerUi, ImportsAnOverlayImageAndMusic) {
    OpenProject p;
    QImage logo(200, 100, QImage::Format_ARGB32);
    logo.fill(QColor(240, 80, 40));
    const auto logoPath = p.fixture.dir / "logo.png";
    ASSERT_TRUE(logo.save(QString::fromStdString(logoPath.string())));
    p.controller.importMedia(QUrl::fromLocalFile(QString::fromStdString(logoPath.string())), "overlay", 1.0);
    ASSERT_TRUE(waitUntil([&] { return !p.controller.busy() && p.controller.selection().value("role") == "overlay"; }));
    EXPECT_NEAR(p.controller.selection().value("start").toDouble(), 1.0, 1e-6);
    EXPECT_NEAR(p.controller.selection().value("duration").toDouble(), 2.0, 1e-6);  // up to the video's end
    EXPECT_NEAR(p.controller.duration(), 3.0, 1e-6);
    EXPECT_TRUE(std::filesystem::exists(p.fixture.dir / "media" / "imported" / "logo.png"));  // copied in

    ASSERT_TRUE(test::writeTestAudio(p.fixture.dir / "song.mkv", {.seconds = 10.0, .channels = 2}));
    p.controller.importMedia(QUrl::fromLocalFile(QString::fromStdString((p.fixture.dir / "song.mkv").string())), "music", 0);
    ASSERT_TRUE(waitUntil([&] { return !p.controller.busy() && p.controller.selection().value("role") == "music"; }));
    EXPECT_NEAR(p.controller.selection().value("duration").toDouble(), 3.0, 1e-6);  // trimmed to the video
    EXPECT_DOUBLE_EQ(p.controller.selection().value("gainDb").toDouble(), -14.0);
    EXPECT_FALSE(p.track("Music").isEmpty());
}

TEST(ProjectControllerUi, FindsAndRemovesPauses) {
    OpenProject p({.seconds = 4.0, .micEnvelope = [](double t) { return t >= 1.0 && t < 2.5 ? 0.0f : 1.0f; }});
    p.controller.findSilences(-42, 0.7, 0.15);
    ASSERT_TRUE(waitUntil([&] { return !p.controller.busy(); }));
    ASSERT_EQ(p.controller.silences().size(), 1);
    EXPECT_NEAR(p.controller.silenceTotal(), 1.2, 0.05);
    p.controller.removeSilences();
    EXPECT_NEAR(p.controller.duration(), 4.0 - 1.2, 0.05);
    EXPECT_EQ(p.controller.silences().size(), 0);
    p.controller.undo();
    EXPECT_NEAR(p.controller.duration(), 4.0, 1e-6);
}

TEST(ProjectControllerUi, ClipKeyframesEvaluateAtPlayhead) {
    OpenProject p;
    const QString id = p.clipId("Screen");
    p.controller.selectClip(id);
    p.controller.setClipKeyframe(id, QStringLiteral("opacity"), 0.0, 1.0, true);
    p.controller.setClipKeyframe(id, QStringLiteral("opacity"), 2.0, 0.25, true);
    const QVariantMap at1 = p.controller.clipTransformAt(id, 1.0);
    EXPECT_TRUE(at1.value(QStringLiteral("onClip")).toBool());
    EXPECT_NEAR(at1.value(QStringLiteral("opacity")).toDouble(), 0.625, 1e-3);
    const QVariantMap at0 = p.controller.clipTransformAt(id, 0.0);
    EXPECT_NEAR(at0.value(QStringLiteral("opacity")).toDouble(), 1.0, 1e-6);
    const QVariantList keys = p.controller.clipKeyframeTimes(id, QStringLiteral("opacity"));
    EXPECT_EQ(keys.size(), 2);
}

TEST(ProjectControllerUi, LayersSelectAddRenameReorderDeleteAndMoveClips) {
    OpenProject o;
    ProjectController& c = o.controller;
    // Selecting a clip selects its layer; a header click selects a layer alone.
    c.selectClip(o.clipId("Camera"));
    EXPECT_EQ(c.selectedTrack(), o.track("Camera").value("id").toString());
    c.clearSelection();
    c.selectTrack(o.track("Microphone").value("id").toString());
    EXPECT_EQ(c.selectedTrack(), o.track("Microphone").value("id").toString());

    // Two text layers: the newest is shown on top.
    c.addText("Hello", 1.0, 2.0, "title");
    const QString first = c.addTrack("overlay");
    ASSERT_FALSE(first.isEmpty());
    EXPECT_EQ(c.selectedTrack(), first);
    EXPECT_EQ(c.tracks().value(0).toMap().value("id").toString(), first);
    c.renameTrack(first, "  Logo  ");
    EXPECT_EQ(c.tracks().value(0).toMap().value("name").toString(), "Logo");
    EXPECT_FALSE(c.tracks().value(0).toMap().value("canMoveUp").toBool());
    EXPECT_TRUE(c.tracks().value(0).toMap().value("canMoveDown").toBool());

    // Move the text clip onto the new layer (one undo step), then back by undo.
    const QString text = o.clipId("Text");
    c.moveClipToTrack(text, first, 3.0);
    EXPECT_EQ(o.clips("Logo").size(), 1);
    EXPECT_NEAR(o.clips("Logo").value(0).toMap().value("start").toDouble(), 3.0, 1e-6);
    EXPECT_TRUE(o.clips("Text").isEmpty());
    c.moveClipToTrack(text, o.track("Microphone").value("id").toString(), 0.0);  // wrong kind: refused
    EXPECT_EQ(o.clips("Logo").size(), 1);
    c.undo();
    EXPECT_EQ(o.clips("Text").size(), 1);
    EXPECT_TRUE(o.clips("Logo").isEmpty());

    // Reorder: the Logo layer goes below Text (stacking order) and back.
    c.moveTrack(first, -1);
    EXPECT_EQ(c.tracks().value(0).toMap().value("name").toString(), "Text");
    EXPECT_EQ(c.tracks().value(1).toMap().value("name").toString(), "Logo");
    c.moveTrack(first, -1);  // only video below: overlays stack above video, refused
    EXPECT_EQ(c.tracks().value(1).toMap().value("name").toString(), "Logo");
    c.moveTrack(first, 1);
    EXPECT_EQ(c.tracks().value(0).toMap().value("name").toString(), "Logo");

    // Delete: the layer goes and so does its selection; undo brings it back.
    c.selectTrack(first);
    c.selectClip(o.clipId("Text"));  // selecting a clip again follows its layer
    EXPECT_EQ(c.selectedTrack(), o.track("Text").value("id").toString());
    c.selectTrack(first);
    c.setTrackValue(first, "hidden", true);  // an edit keeps the picked layer
    EXPECT_EQ(c.selectedTrack(), first);
    c.setTrackValue(first, "hidden", false);
    const int before = static_cast<int>(c.tracks().size());
    c.deleteTrack(first);
    EXPECT_EQ(c.tracks().size(), before - 1);
    EXPECT_TRUE(c.selectedTrack().isEmpty());
    c.undo();
    EXPECT_EQ(c.tracks().size(), before);
    const project::Project saved = o.saved();
    EXPECT_TRUE(saved.validate());
}

TEST(ProjectControllerUi, AutoBalanceRecoversAKnownCast) {
    OpenProject o;
    ProjectController& c = o.controller;
    const QString clip = o.clipId("Screen");
    // A neutral gray source shown with Temp 0.4, Tint −0.3: the channel gains of colorCurves.
    c.setColorValue(clip, "temperature", 0.4);
    c.setColorValue(clip, "tint", -0.3);
    const double gray = 0.5;
    const double r = gray * (1.0 + 0.15 * 0.4 + 0.05 * -0.3), g = gray * (1.0 - 0.10 * -0.3), b = gray * (1.0 - 0.15 * 0.4 + 0.05 * -0.3);
    c.autoBalance(clip, r, g, b);
    c.selectClip(clip);
    EXPECT_NEAR(c.selection().value("temperature").toDouble(), 0.0, 0.011);
    EXPECT_NEAR(c.selection().value("tint").toDouble(), 0.0, 0.011);
    c.undo();
    c.selectClip(clip);
    EXPECT_NEAR(c.selection().value("temperature").toDouble(), 0.4, 1e-9);
}

TEST(ProjectControllerUi, LooksApplyMixUndoAndApplyToAll) {
    OpenProject o;
    ProjectController& c = o.controller;
    const QString screen = o.clipId("Screen");
    const QString camera = o.clipId("Camera");
    QStringList ids;
    for (const QVariant& v : c.looks()) ids << v.toMap().value("id").toString();
    EXPECT_GE(ids.size(), 16);
    EXPECT_TRUE(ids.contains("oppenheimer"));
    EXPECT_TRUE(ids.contains("dark-green"));

    c.applyLook(screen, "dark-green", 0.8);
    c.selectClip(screen);
    EXPECT_EQ(c.selection().value("look").toString(), "dark-green");
    EXPECT_EQ(c.selection().value("lookName").toString(), "Dark Green");
    EXPECT_NEAR(c.selection().value("lookAmount").toDouble(), 0.8, 1e-9);
    EXPECT_EQ(c.undoLabel(), "Look: Dark Green");
    // The look reaches the renderers on top of the (neutral) correction.
    for (const auto& l : editor::buildRenderPlan(*c.snapshot(), Time::fromSeconds(1)).layers) {
        if (l.role == "screen") EXPECT_NEAR(l.color.tint, -0.35 * 0.8, 1e-9);
    }
    // Amount drags merge into one undo step.
    for (int i = 1; i <= 10; ++i) c.setLookAmount(screen, i * 0.05);
    c.selectClip(screen);
    EXPECT_NEAR(c.selection().value("lookAmount").toDouble(), 0.5, 1e-9);
    c.undo();
    c.selectClip(screen);
    EXPECT_NEAR(c.selection().value("lookAmount").toDouble(), 0.8, 1e-9);

    c.applyLookToAll(screen);
    c.selectClip(camera);
    EXPECT_EQ(c.selection().value("look").toString(), "dark-green");
    c.undo();
    c.selectClip(camera);
    EXPECT_EQ(c.selection().value("look").toString(), "");

    // An unknown look changes nothing; removing the look keeps the correction.
    c.setColorValue(screen, "exposure", 0.3);
    c.applyLook(screen, "no-such-look", 1.0);
    c.selectClip(screen);
    EXPECT_EQ(c.selection().value("look").toString(), "dark-green");
    c.removeLook(screen);
    c.selectClip(screen);
    EXPECT_EQ(c.selection().value("look").toString(), "");
    EXPECT_DOUBLE_EQ(c.selection().value("exposure").toDouble(), 0.3);

    // Hover preview: only the shown snapshot changes, never the document.
    c.previewLook(screen, "noir");
    for (const auto& l : editor::buildRenderPlan(*c.snapshot(), Time::fromSeconds(1)).layers) {
        if (l.role == "screen") EXPECT_DOUBLE_EQ(l.color.saturation, -1.0);
    }
    c.selectClip(screen);
    EXPECT_EQ(c.selection().value("look").toString(), "");
    c.cancelPreview();
    for (const auto& l : editor::buildRenderPlan(*c.snapshot(), Time::fromSeconds(1)).layers) {
        if (l.role == "screen") EXPECT_DOUBLE_EQ(l.color.saturation, 0.0);
    }
}

TEST(ProjectControllerUi, SavedLooksPersistAcrossControllersAndDelete) {
    OpenProject o;
    ProjectController& c = o.controller;
    const QString screen = o.clipId("Screen");
    c.setColorValue(screen, "temperature", 0.25);
    c.setColorWheel(screen, "gain", 0.1, -0.05, 0.0);
    c.applyLook(screen, "warm-film", 0.5);
    int changes = 0;
    QObject::connect(&c, &ProjectController::looksChanged, [&] { ++changes; });
    EXPECT_TRUE(c.saveLook(screen, "   ").isEmpty());  // a name is required
    const QString id = c.saveLook(screen, "My Warm Day");
    ASSERT_TRUE(id.startsWith("custom-")) << id.toStdString();
    EXPECT_EQ(changes, 1);

    // A second controller (another project window, a restart) sees it under My Looks.
    ProjectController other;
    QVariantMap saved;
    for (const QVariant& v : other.looks()) {
        if (v.toMap().value("id").toString() == id) saved = v.toMap();
    }
    EXPECT_EQ(saved.value("name").toString(), "My Warm Day");
    EXPECT_EQ(saved.value("category").toString(), "My Looks");
    EXPECT_TRUE(saved.value("custom").toBool());

    // Applying it on another clip reproduces the whole grade (correction + look).
    const QString camera = o.clipId("Camera");
    c.applyLook(camera, id, 1.0);
    editor::ColorParams screenGrade;
    editor::ColorParams cameraGrade;
    for (const auto& l : editor::buildRenderPlan(*c.snapshot(), Time::fromSeconds(1)).layers) {
        if (l.role == "screen") screenGrade = l.color;
        if (l.role == "camera") cameraGrade = l.color;
    }
    EXPECT_NEAR(cameraGrade.temperature, screenGrade.temperature, 1e-9);
    EXPECT_NEAR(cameraGrade.gain.x, screenGrade.gain.x, 1e-9);
    EXPECT_NEAR(cameraGrade.contrast, screenGrade.contrast, 1e-9);

    c.deleteLook("oppenheimer");  // built-in looks stay
    c.deleteLook(id);
    EXPECT_EQ(changes, 2);
    ProjectController third;
    for (const QVariant& v : third.looks()) EXPECT_NE(v.toMap().value("id").toString(), id);
    // Projects keep a look's settings, so the clip still shows it.
    c.selectClip(camera);
    EXPECT_EQ(c.selection().value("look").toString(), id);
}

TEST(ProjectControllerUi, CopyPasteAndSpreadGrades) {
    test::EditorFixture::Options options;
    OpenProject o(options);
    ProjectController& c = o.controller;
    const QString screen = o.clipId("Screen");
    const QString camera = o.clipId("Camera");
    c.splitAt(1.5);  // two screen and two camera clips
    const QStringList order = c.gradableClips();
    ASSERT_EQ(order.size(), 4) << order.join(",").toStdString();
    const QString first = order[0];

    c.setColorValue(first, "exposure", 0.4);
    c.setCurve(first, "y", QVariantList{QVariantMap{{"x", 0.0}, {"y", 0.1}}, QVariantMap{{"x", 1.0}, {"y", 0.9}}});
    c.applyLook(first, "teal-orange", 0.7);
    c.setInputColorSpace(order[3], "rec2020");

    EXPECT_FALSE(c.hasCopiedGrade());
    c.pasteGrade({order[1]});  // nothing copied yet
    c.selectClip(order[1]);
    EXPECT_DOUBLE_EQ(c.selection().value("exposure").toDouble(), 0.0);

    c.copyGrade(first);
    EXPECT_TRUE(c.hasCopiedGrade());
    c.pasteGrade({order[2], order[3]});
    EXPECT_EQ(c.undoLabel(), "Paste grade to 2 clips");
    for (const QString& id : {order[2], order[3]}) {
        c.selectClip(id);
        EXPECT_DOUBLE_EQ(c.selection().value("exposure").toDouble(), 0.4);
        EXPECT_EQ(c.selection().value("look").toString(), "teal-orange");
        EXPECT_EQ(c.selection().value("curveY").toList().size(), 2);
    }
    c.selectClip(order[3]);
    EXPECT_EQ(c.selection().value("inputColorSpace").toString(), "rec2020");  // the clip's own setting stays
    c.undo();
    c.selectClip(order[2]);
    EXPECT_DOUBLE_EQ(c.selection().value("exposure").toDouble(), 0.0);

    c.applyGradeToNext(first);
    c.selectClip(order[1]);
    EXPECT_DOUBLE_EQ(c.selection().value("exposure").toDouble(), 0.4);
    c.applyGradeToNext(order[3]);  // the last clip has no next one
    EXPECT_FALSE(c.message().isEmpty());

    c.applyPreviousGrade(order[2]);  // takes order[1]'s (= first's) grade
    c.selectClip(order[2]);
    EXPECT_EQ(c.selection().value("look").toString(), "teal-orange");
    c.applyPreviousGrade(first);  // nothing before the first clip
    c.selectClip(first);
    EXPECT_DOUBLE_EQ(c.selection().value("exposure").toDouble(), 0.4);

    c.resetColor(order[3]);
    c.selectClip(order[3]);
    EXPECT_EQ(c.selection().value("inputColorSpace").toString(), "rec2020");  // reset keeps how the source is read
    c.applyGradeToAll(first);
    EXPECT_EQ(c.undoLabel(), "Grade to all clips");
    for (const QString& id : order) {
        c.selectClip(id);
        EXPECT_DOUBLE_EQ(c.selection().value("exposure").toDouble(), 0.4) << id.toStdString();
    }
    (void)screen;
    (void)camera;
}

TEST(ProjectControllerUi, LookThumbnailsRenderInTheBackground) {
    OpenProject o;
    ProjectController& c = o.controller;
    const QString screen = o.clipId("Screen");
    c.setColorValue(screen, "exposure", 0.2);
    const int before = c.lookPreviewRevision();
    c.refreshLookPreviews(screen, 1.0);
    ASSERT_TRUE(waitUntil([&] { return c.lookPreviewRevision() > before; }));
    const QImage none = lookpreviews::image("none");
    const QImage noir = lookpreviews::image("noir");
    const QImage green = lookpreviews::image("dark-green");
    ASSERT_FALSE(none.isNull());
    ASSERT_FALSE(noir.isNull());
    EXPECT_EQ(none.height(), 135);
    // Noir is gray; Dark Green is greener than the clip without a look.
    double gray = 0, greenGain = 0;
    for (int y = 0; y < none.height(); y += 4) {
        for (int x = 0; x < none.width(); x += 4) {
            const QColor n = noir.pixelColor(x, y);
            gray += std::abs(n.red() - n.green()) + std::abs(n.green() - n.blue());
            const QColor a = none.pixelColor(x, y);
            const QColor g = green.pixelColor(x, y);
            greenGain += (g.green() - (g.red() + g.blue()) / 2.0) - (a.green() - (a.red() + a.blue()) / 2.0);
        }
    }
    EXPECT_LT(gray / (none.width() * none.height() / 16.0), 2.0);
    EXPECT_GT(greenGain / (none.width() * none.height() / 16.0), 5.0);
}

TEST(ProjectControllerUi, NodesGradePartsOfThePicture) {
    OpenProject o;
    ProjectController& c = o.controller;
    const QString screen = o.clipId("Screen");
    auto nodes = [&] {
        c.selectClip(screen);
        return c.selection().value("nodes").toList();
    };
    auto screenLayer = [&] {
        for (const auto& l : editor::buildRenderPlan(*c.snapshot(), Time::fromSeconds(1)).layers) {
            if (l.role == "screen") return l;
        }
        return editor::VisualLayer{};
    };

    const QString circle = c.addNode(screen, "circle");
    EXPECT_EQ(circle, "n2");  // node 01 is the clip's own correction
    ASSERT_EQ(nodes().size(), 1);
    QVariantMap n = nodes()[0].toMap();
    EXPECT_EQ(n.value("label").toString(), "Circle");
    EXPECT_EQ(n.value("window").toMap().value("shape").toString(), "circle");
    // Round on the 16:9 screen recording.
    EXPECT_NEAR(n.value("window").toMap().value("height").toDouble(), 0.35 * 640.0 / 360.0, 1e-9);

    for (int i = 1; i <= 10; ++i) c.setNodeValue(screen, circle, "exposure", i * 0.05);  // a slider drag
    c.setNodeWheel(screen, circle, "gain", 0.2, 0.1, 0.05);
    c.setNodeCurve(screen, circle, "y", QVariantList{QVariantMap{{"x", 0.0}, {"y", 0.1}}, QVariantMap{{"x", 1.0}, {"y", 1.0}}});
    c.setNodeWindow(screen, circle, QVariantMap{{"x", 0.3}, {"softness", 0.5}, {"rotation", 20}});
    n = nodes()[0].toMap();
    EXPECT_DOUBLE_EQ(n.value("exposure").toDouble(), 0.5);
    EXPECT_DOUBLE_EQ(n.value("gain").toMap().value("x").toDouble(), 0.2);
    EXPECT_EQ(n.value("curveY").toList().size(), 2);
    EXPECT_DOUBLE_EQ(n.value("window").toMap().value("x").toDouble(), 0.3);
    EXPECT_DOUBLE_EQ(n.value("window").toMap().value("rotation").toDouble(), 20.0);
    // The clip's own correction is untouched; the renderers get the node.
    c.selectClip(screen);
    EXPECT_DOUBLE_EQ(c.selection().value("exposure").toDouble(), 0.0);
    editor::VisualLayer l = screenLayer();
    ASSERT_EQ(l.nodes.size(), 1u);
    EXPECT_NEAR(l.nodes[0].grade.exposure, 0.5, 1e-12);
    EXPECT_EQ(l.nodes[0].window.shape, "circle");
    c.undo();  // the exposure drag was one step
    c.undo();
    c.undo();
    c.undo();
    EXPECT_DOUBLE_EQ(nodes()[0].toMap().value("exposure").toDouble(), 0.0);

    const QString person = c.addNode(screen, "person");
    const QString key = c.addNode(screen, "color");
    ASSERT_EQ(nodes().size(), 3);
    EXPECT_EQ(nodes()[1].toMap().value("subject").toString(), "person");
    EXPECT_TRUE(nodes()[2].toMap().value("qualifier").toMap().value("enabled").toBool());
    c.setNodeQualifier(screen, key, QVariantMap{{"satLow", 0.9}, {"satHigh", 0.2}});  // swapped into order
    EXPECT_DOUBLE_EQ(nodes()[2].toMap().value("qualifier").toMap().value("satLow").toDouble(), 0.2);

    c.setNodeEnabled(screen, person, false);
    EXPECT_EQ(screenLayer().nodes.size(), 2u);  // disabled nodes are not rendered
    c.moveNode(screen, key, -1);
    EXPECT_EQ(nodes()[1].toMap().value("id").toString(), key);
    c.setNodeSubject(screen, person, "background");
    c.setNodeInvert(screen, person, true);
    c.setNodeLabel(screen, person, "  Room  ");
    n = nodes()[2].toMap();
    EXPECT_EQ(n.value("subject").toString(), "background");
    EXPECT_TRUE(n.value("invert").toBool());
    EXPECT_EQ(n.value("label").toString(), "Room");

    // At most eight nodes per clip.
    for (int i = 0; i < 5; ++i) EXPECT_FALSE(c.addNode(screen).isEmpty());
    EXPECT_TRUE(c.addNode(screen).isEmpty());
    EXPECT_FALSE(c.message().isEmpty());
    EXPECT_TRUE(c.addNode(screen, "triangle").isEmpty());

    c.removeNode(screen, circle);
    EXPECT_EQ(nodes().size(), 7);
    c.undo();
    EXPECT_EQ(nodes().size(), 8);
    c.resetNode(screen, circle);
    EXPECT_EQ(nodes()[0].toMap().value("window").toMap().value("shape").toString(), "circle");  // the selection stays

    // Copying a grade takes its nodes along.
    const QString camera = o.clipId("Camera");
    c.copyGradeTo(screen, {camera});
    c.selectClip(camera);
    EXPECT_EQ(c.selection().value("nodes").toList().size(), 8);
}

TEST(ProjectControllerUi, PickingAColorKeysTheNodeOnThatColor) {
    OpenProject o;
    ProjectController& c = o.controller;
    const QString screen = o.clipId("Screen");
    const QVariantMap frame = c.sourceFrame(screen, 1.0);
    ASSERT_FALSE(frame.isEmpty());
    // The screen fits its box: the whole source is visible.
    EXPECT_NEAR(frame.value("w").toDouble(), frame.value("visibleW").toDouble(), 1e-9);
    EXPECT_NEAR(frame.value("aspect").toDouble(), 640.0 / 360.0, 1e-9);
    const QString key = c.addNode(screen, "color");
    const double x = frame.value("x").toDouble() + frame.value("w").toDouble() * 0.3;
    const double y = frame.value("y").toDouble() + frame.value("h").toDouble() * 0.6;
    ASSERT_TRUE(c.pickNodeColor(screen, key, x, y, 1.0));
    EXPECT_EQ(c.undoLabel(), "Pick color");

    // The key selects the screen's color, not the camera's.
    c.selectClip(screen);
    const QVariantMap q = c.selection().value("nodes").toList()[0].toMap().value("qualifier").toMap();
    EXPECT_TRUE(q.value("enabled").toBool());
    editor::NodeParams node;
    for (const auto& l : editor::buildRenderPlan(*c.snapshot(), Time::fromSeconds(1)).layers) {
        if (l.role == "screen") node = l.nodes.at(0);
    }
    const QImage shown = ui::grabFrame(QString::fromStdString((o.fixture.dir / "screen.mkv").string()), 1.0, 90)
                             .convertToFormat(QImage::Format_RGB32);
    const QImage other = ui::grabFrame(QString::fromStdString((o.fixture.dir / "camera.mkv").string()), 1.0, 90)
                             .convertToFormat(QImage::Format_RGB32);
    ASSERT_FALSE(shown.isNull());
    const QColor a = shown.pixelColor(10, 10);
    const QColor b = other.pixelColor(10, 10);
    EXPECT_GT(editor::nodeMatte(node, 0.5, 0.5, 1, a.redF(), a.greenF(), a.blueF(), -1), 0.95);
    EXPECT_LT(editor::nodeMatte(node, 0.5, 0.5, 1, b.redF(), b.greenF(), b.blueF(), -1), 0.05);

    // Off the clip's picture: nothing to pick.
    const QString camera = o.clipId("Camera");
    const QString camKey = c.addNode(camera, "color");
    EXPECT_FALSE(c.pickNodeColor(camera, camKey, 0.01, 0.99, 1.0));
}

TEST(ProjectControllerUi, HslCurvesOnTheCorrectionAndOnNodes) {
    OpenProject o;
    ProjectController& c = o.controller;
    const QString screen = o.clipId("Screen");
    const QVariantList greens{QVariantMap{{"x", 0.2}, {"y", 0.5}}, QVariantMap{{"x", 0.333}, {"y", 0.1}},
                              QVariantMap{{"x", 0.46}, {"y", 0.5}}};
    c.setHslCurve(screen, "hueVsSat", greens);
    c.selectClip(screen);
    EXPECT_EQ(c.selection().value("hueVsSat").toList().size(), 3);
    EXPECT_TRUE(c.selection().value("hueVsHue").toList().isEmpty());
    for (const auto& l : editor::buildRenderPlan(*c.snapshot(), Time::fromSeconds(1)).layers) {
        if (l.role == "screen") EXPECT_EQ(l.color.hsl[timeline::kHueVsSat].size(), 3u);
    }
    // Drags merge into one step (another edit in between starts a new gesture);
    // unknown curves are ignored; one point is no curve.
    c.setColorValue(screen, "exposure", 0.1);
    for (int i = 0; i < 5; ++i) c.setHslCurve(screen, "hueVsSat", {greens[0], QVariantMap{{"x", 0.333}, {"y", 0.1 + i * 0.02}}, greens[2]});
    c.setHslCurve(screen, "hueVsBanana", greens);
    c.undo();
    c.selectClip(screen);
    const QVariantList afterUndo = c.selection().value("hueVsSat").toList();
    ASSERT_EQ(afterUndo.size(), 3);
    EXPECT_NEAR(afterUndo[1].toMap().value("y").toDouble(), 0.1, 1e-9);
    c.setHslCurve(screen, "lumVsSat", {QVariantMap{{"x", 0.5}, {"y", 0.2}}});
    c.selectClip(screen);
    EXPECT_TRUE(c.selection().value("lumVsSat").toList().isEmpty());
    // A flat line is kept while editing but renders as nothing.
    c.setHslCurve(screen, "satVsLum", {QVariantMap{{"x", 0.0}, {"y", 0.5}}, QVariantMap{{"x", 1.0}, {"y", 0.5}}});
    for (const auto& l : editor::buildRenderPlan(*c.snapshot(), Time::fromSeconds(1)).layers) {
        if (l.role == "screen") EXPECT_TRUE(l.color.hsl[timeline::kSatVsLum].empty());
    }
    // Samples wrap for hue curves.
    const QVariantList samples = c.hslCurveSamples(greens, "hueVsSat", 7);
    ASSERT_EQ(samples.size(), 7);
    EXPECT_NEAR(samples[2].toDouble(), 0.1, 0.03);  // x = 1/3
    EXPECT_NEAR(samples[0].toDouble(), 0.5, 1e-9);
    // Nodes have their own HSL curves.
    const QString node = c.addNode(screen, "circle");
    c.setNodeHslCurve(screen, node, "hueVsHue", greens);
    c.selectClip(screen);
    const QVariantList nodes = c.selection().value("nodes").toList();
    ASSERT_EQ(nodes.size(), 1);
    EXPECT_EQ(nodes[0].toMap().value("hueVsHue").toList().size(), 3);
    // The picker reads the picture there (the screen is one flat color).
    const QVariantMap frame = c.sourceFrame(screen, 1.0);
    const QVariantMap color = c.colorAt(screen, frame.value("x").toDouble() + frame.value("w").toDouble() * 0.4,
                                        frame.value("y").toDouble() + frame.value("h").toDouble() * 0.4, 1.0);
    ASSERT_TRUE(color.contains("hue"));
    EXPECT_GE(color.value("hue").toDouble(), 0.0);
    EXPECT_LE(color.value("hue").toDouble(), 1.0);
    EXPECT_TRUE(c.colorAt(o.clipId("Camera"), 0.01, 0.99, 1.0).isEmpty());  // not on the camera there
}

TEST(PlaybackControllerUi, PlaysSeeksAndRendersFrames) {
    OpenProject p;
    PlaybackController playback(&p.controller, /*silent=*/true);
    playback.setPreviewSize({320, 180});
    ASSERT_TRUE(waitUntil([&] { return !playback.frame().isNull(); }));
    EXPECT_EQ(playback.frame().size(), QSize(320, 180));
    playback.play();
    ASSERT_TRUE(waitUntil([&] { return playback.playing(); }));
    ASSERT_TRUE(waitUntil([&] { return playback.position() > 0.4; }, 3000));
    playback.pause();
    ASSERT_TRUE(waitUntil([&] { return !playback.playing(); }));
    playback.seek(2.0);
    EXPECT_DOUBLE_EQ(playback.position(), 2.0);
    playback.step(3);
    EXPECT_NEAR(playback.position(), 2.1, 1e-9);
    // Edits reach the running engine: the playhead clamps to a shorter video.
    p.controller.removeRange(0.0, 2.5);
    ASSERT_TRUE(waitUntil([&] { return playback.position() <= 0.5 + 1e-9; }));
}

TEST(PlaybackControllerUi, ComparesGradedAndUngraded) {
    OpenProject p;
    const QString screen = p.clipId("Screen");
    PlaybackController playback(&p.controller, /*silent=*/true);
    playback.setPreviewSize({320, 180});
    playback.seek(1.0);
    ASSERT_TRUE(waitUntil([&] { return !playback.frame().isNull(); }));
    // The frame after a change: wait until it differs from `old`, then until
    // two frames in a row agree (the engine finished re-rendering).
    auto next = [&](const QImage& old) {
        waitUntil([&] { return playback.frame() != old; }, 3000);
        QImage last = playback.frame();
        for (int i = 0; i < 40; ++i) {
            waitUntil([] { return false; }, 25);
            const QImage now = playback.frame();
            if (now == last) break;
            last = now;
        }
        return last;
    };
    const QImage plain = next(QImage());
    p.controller.applyLook(screen, "noir", 1.0);
    const QImage graded = next(plain);
    ASSERT_NE(graded, plain);

    playback.setCompareMode("bypass");
    EXPECT_EQ(playback.compareMode(), "bypass");
    const QImage bypass = next(graded);
    EXPECT_EQ(bypass, plain);  // every grade off

    playback.setCompareMode("wipe");
    playback.setCompareSplit(0.5);
    EXPECT_DOUBLE_EQ(playback.compareSplit(), 0.5);
    const QImage wipe = next(bypass);
    // Ungraded left of the middle, graded right of it.
    EXPECT_EQ(wipe.copy(0, 0, 150, 180), plain.copy(0, 0, 150, 180));
    EXPECT_EQ(wipe.copy(170, 0, 150, 180), graded.copy(170, 0, 150, 180));

    playback.setCompareMode("side");
    const QImage side = next(wipe);
    EXPECT_EQ(side.pixelColor(80, 10), QColor(Qt::black));  // letterbox above the halves
    EXPECT_NE(side.copy(0, 45, 160, 90), side.copy(160, 45, 160, 90));

    playback.setCompareMode("nonsense");
    EXPECT_EQ(playback.compareMode(), "off");
    EXPECT_EQ(next(side), graded);
}

TEST(PlaybackControllerUi, HighlightShowsANodesSelection) {
    OpenProject p;
    const QString screen = p.clipId("Screen");
    const QString node = p.controller.addNode(screen, "circle");
    PlaybackController playback(&p.controller, /*silent=*/true);
    playback.setPreviewSize({320, 180});
    playback.seek(1.0);
    ASSERT_TRUE(waitUntil([&] { return !playback.frame().isNull(); }));
    auto next = [&](const QImage& old) {
        waitUntil([&] { return playback.frame() != old; }, 3000);
        QImage last = playback.frame();
        for (int i = 0; i < 40; ++i) {
            waitUntil([] { return false; }, 25);
            const QImage now = playback.frame();
            if (now == last) break;
            last = now;
        }
        return last;
    };
    const QImage normal = next(QImage());
    playback.setHighlight(screen, node);
    EXPECT_EQ(playback.highlightNode(), node);
    const QImage highlight = next(normal);
    // Outside the circle, the screen turns mid gray; the center keeps its color.
    const QVariantMap frame = p.controller.sourceFrame(screen, 1.0);
    const int cx = static_cast<int>((frame.value("x").toDouble() + frame.value("w").toDouble() * 0.5) * 320);
    const int cy = static_cast<int>((frame.value("y").toDouble() + frame.value("h").toDouble() * 0.5) * 180);
    const int ex = static_cast<int>((frame.value("x").toDouble() + frame.value("w").toDouble() * 0.04) * 320);
    const int ey = static_cast<int>((frame.value("y").toDouble() + frame.value("h").toDouble() * 0.06) * 180);
    EXPECT_EQ(highlight.pixelColor(cx, cy), normal.pixelColor(cx, cy));
    EXPECT_EQ(highlight.pixelColor(ex, ey), QColor(128, 128, 128));
    playback.setHighlight(screen, QString());
    EXPECT_EQ(next(highlight), normal);
}

TEST(ExportControllerUi, ExportsTheProjectInTheBackground) {
    OpenProject p;
    ExportController exporter(&p.controller);
    const QVariantList sizes = exporter.resolutions();
    ASSERT_EQ(sizes.size(), 4);
    EXPECT_EQ(sizes[0].toMap().value("width").toInt(), 1280);
    EXPECT_GT(exporter.estimateMegabytes("720p", 30, "high"), 0.5);
    const auto out = p.fixture.dir / "out" / "video.mp4";
    exporter.start("720p", 30, "standard", QUrl::fromLocalFile(QString::fromStdString(out.string())).toString());
    EXPECT_TRUE(exporter.running());
    ASSERT_TRUE(waitUntil([&] { return !exporter.running(); }, 60'000));
    EXPECT_TRUE(exporter.finished()) << exporter.error().toStdString();
    EXPECT_EQ(exporter.outputPath().toStdString(), out.string());
    auto info = media::probeMedia(out);
    ASSERT_TRUE(info) << info.error().toString();
    EXPECT_NEAR(info->duration.toSecondsF(), 3.0, 0.1);
    EXPECT_EQ(info->video()->video->width, 1280);
}
