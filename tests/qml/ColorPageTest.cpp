// The Color page with real mouse input: wheels (puck and master jog),
// scrubby number fields, double-click reset, scope modes and undo.

#include "editor/EditorFixture.h"
#include "editor/RenderPlan.h"
#include "project/ProjectStore.h"
#include "qml/QmlHarness.h"
#include "ui/PlaybackController.h"
#include "ui/ProjectController.h"

#include <gtest/gtest.h>

#include <cstdlib>

using namespace lectern;
using namespace lectern::ui;
using test::QmlHarness;

namespace {

const QByteArray kPage = R"(
import QtQuick
import Lectern.UI
Item {
    required property ProjectController project
    required property PlaybackController playback
    ColorPage { anchors.fill: parent; project: parent.project; playback: parent.playback }
}
)";

struct Page {
    test::EditorFixture fixture;
    ProjectController project;
    std::unique_ptr<PlaybackController> playback;
    std::unique_ptr<QmlHarness> ui;
    Page() {
        EXPECT_TRUE(project::ProjectStore::save(fixture.dir.path(), fixture.project));
        project.open(QString::fromStdString(fixture.dir.path().string()));
        EXPECT_TRUE(test::waitFor([this] { return project.loaded(); }));
        playback = std::make_unique<PlaybackController>(&project, /*silent=*/true);
        playback->seek(1.0);
        ui = std::make_unique<QmlHarness>(kPage, QVariantMap{{"project", QVariant::fromValue(&project)},
                                                             {"playback", QVariant::fromValue(playback.get())}},
                                          QSize(1400, 950));
        test::settle(150);
    }
    QPoint center(const char* name) {
        QQuickItem* item = ui->find(name);
        EXPECT_TRUE(item) << name;
        return item ? QmlHarness::at(item, {item->width() / 2, item->height() / 2}) : QPoint();
    }
    /// The value box of a ScrubField (right of its label).
    QPoint field(const char* name) {
        QQuickItem* item = ui->find(name);
        EXPECT_TRUE(item) << name;
        return item ? QmlHarness::at(item, {item->width() - 31, item->height() / 2}) : QPoint();
    }
    QVariantMap wheel(const char* key) { return project.selection().value(key).toMap(); }
};

}  // namespace

TEST(ColorPage, SelectsAClipToGradeAutomatically) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    EXPECT_FALSE(p.project.selectedClip().isEmpty());
    EXPECT_TRUE(p.project.selection().value("visual").toBool());
}

TEST(ColorPage, WheelPuckAndMasterJogChangeTheGradeAsOneUndoStepEach) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    QQuickItem* dial = p.ui->find("wheel-gain");
    ASSERT_TRUE(dial);
    const QPoint c = p.center("wheel-gain");
    const int r = static_cast<int>(std::min(dial->width(), dial->height()) / 2 - 14);
    // Push the gain toward the right (Cb+, blue-ish) and up (Cr+, red-ish).
    p.ui->drag(c, c + QPoint(r / 2, -r / 2));
    const QVariantMap gain = p.wheel("gain");
    EXPECT_GT(gain.value("x").toDouble(), 0.3);
    EXPECT_GT(gain.value("y").toDouble(), 0.3);
    // The master jog changes luminance only.
    const QPoint jog = p.center("master-gain");
    p.ui->drag(jog, jog + QPoint(60, 0));
    EXPECT_GT(p.wheel("gain").value("master").toDouble(), 0.2);
    EXPECT_NEAR(p.wheel("gain").value("x").toDouble(), gain.value("x").toDouble(), 1e-9);
    // Undo removes the jog, then the puck drag (each drag merged into one step).
    p.project.undo();
    EXPECT_NEAR(p.wheel("gain").value("master").toDouble(), 0.0, 1e-9);
    p.project.undo();
    EXPECT_NEAR(p.wheel("gain").value("x").toDouble(), 0.0, 1e-9);
}

TEST(ColorPage, ScrubFieldsDragDoubleClickResetsAndTheOffsetWheelWorks) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    const QPoint contrast = p.field("contrast");
    p.ui->drag(contrast, contrast + QPoint(80, 0));
    const double v = p.project.selection().value("contrast").toDouble();
    EXPECT_GT(v, 0.2);
    QTest::mouseDClick(&p.ui->window(), Qt::LeftButton, {}, contrast);
    test::settle(60);
    EXPECT_NEAR(p.project.selection().value("contrast").toDouble(), 0.0, 1e-9);
    // Pivot defaults to 0.5 and resets there.
    const QPoint pivot = p.field("pivot");
    p.ui->drag(pivot, pivot + QPoint(-60, 0));
    EXPECT_LT(p.project.selection().value("pivot").toDouble(), 0.45);
    QTest::mouseDClick(&p.ui->window(), Qt::LeftButton, {}, pivot);
    test::settle(60);
    EXPECT_NEAR(p.project.selection().value("pivot").toDouble(), 0.5, 1e-9);
    test::settle(450);
    EXPECT_FALSE(p.ui->window().activeFocusItem() && p.ui->window().activeFocusItem()->inherits("QQuickTextInput"))
        << "a double-click must not leave the field in typing mode";
    // Offset wheel.
    const QPoint off = p.center("wheel-offset");
    p.ui->drag(off, off + QPoint(-30, 0));
    EXPECT_LT(p.wheel("offset").value("x").toDouble(), -0.1);
    // A single click turns the field into a text box: type an exact value.
    const QPoint sat = p.field("saturation");
    p.ui->click(sat);
    test::settle(450);  // after the double-click interval
    QTest::keyClick(&p.ui->window(), Qt::Key_A, Qt::ControlModifier);
    p.ui->type(QStringLiteral("-0.25"));
    QTest::keyClick(&p.ui->window(), Qt::Key_Return);
    test::settle(60);
    EXPECT_NEAR(p.project.selection().value("saturation").toDouble(), -0.25, 1e-9);
    // Reset grade clears everything.
    p.ui->click(p.center("resetGrade"));
    EXPECT_NEAR(p.wheel("offset").value("x").toDouble(), 0.0, 1e-9);
}

TEST(ColorPage, ScopesFollowThePictureAndSwitchModes) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    QQuickItem* scope = p.ui->find("scope");
    ASSERT_TRUE(scope);
    ASSERT_TRUE(test::waitFor([&] { return !scope->property("stats").toMap().isEmpty(); }, 5000));
    const double white0 = scope->property("stats").toMap().value("white").toDouble();
    // Brighten with Gain master: the scopes see a brighter picture.
    const QPoint jog = p.center("master-gain");
    p.ui->drag(jog, jog + QPoint(120, 0));
    ASSERT_TRUE(test::waitFor([&] { return scope->property("stats").toMap().value("white").toDouble() > white0 + 20; }, 5000))
        << "white " << white0 << " -> " << scope->property("stats").toMap().value("white").toDouble();
    for (const char* mode : {"waveform", "vectorscope", "histogram", "parade"}) {
        scope->setProperty("mode", QString::fromLatin1(mode));
        test::settle(20);
        EXPECT_EQ(scope->property("mode").toString(), mode);
    }
}

TEST(ColorPage, CurvesAddDragAndRemovePointsPerChannel) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    p.ui->click(p.center("palette-curves"));
    QQuickItem* editor = p.ui->find("curveEditor");
    ASSERT_TRUE(editor && editor->isVisible());
    const double w = editor->width() - 20, h = editor->height() - 20;  // the plot area (10 px margins)
    auto at = [&](double x, double y) { return QmlHarness::at(editor, {10 + x * w, 10 + (1 - y) * h}); };
    // Click on the diagonal at 0.5 and drag the new point up to 0.7: a brighter midtone curve.
    p.ui->drag(at(0.5, 0.5), at(0.5, 0.7));
    QVariantList y = p.project.selection().value("curveY").toList();
    ASSERT_EQ(y.size(), 3);
    EXPECT_NEAR(y[1].toMap().value("y").toDouble(), 0.7, 0.02);
    EXPECT_NEAR(y[1].toMap().value("x").toDouble(), 0.5, 0.02);
    // The engine curve passes through the point and stays monotone.
    const QVariantList samples = p.project.curveSamples(y, 11);
    EXPECT_NEAR(samples[5].toDouble(), 0.7, 0.02);
    for (int i = 1; i < samples.size(); ++i) EXPECT_GE(samples[i].toDouble() + 1e-9, samples[i - 1].toDouble());
    // Red channel has its own curve.
    p.ui->click(p.center("curve-r"));
    p.ui->drag(at(0.75, 0.75), at(0.75, 0.6));
    EXPECT_EQ(p.project.selection().value("curveR").toList().size(), 3);
    EXPECT_EQ(p.project.selection().value("curveY").toList().size(), 3);  // luma unchanged
    // Double-click removes the point; a straight curve is stored as no curve.
    QTest::mouseDClick(&p.ui->window(), Qt::LeftButton, {}, at(0.75, 0.6));
    test::settle(60);
    EXPECT_EQ(p.project.selection().value("curveR").toList().size(), 2);
    // Reset curves clears all channels.
    p.ui->click(p.center("resetGrade"));
    EXPECT_TRUE(p.project.selection().value("curveY").toList().isEmpty());
    EXPECT_TRUE(p.project.selection().value("curveR").toList().isEmpty());
    if (const char* dump = std::getenv("LECTERN_DUMP_DIR")) {
        p.ui->drag(at(0.5, 0.5), at(0.5, 0.68));
        p.ui->window().grabWindow().save(QString::fromLocal8Bit(dump) + "/curves.png");
    }
}

TEST(ColorPage, AutoBalanceRemovesAColorCast) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    // Give the clip a strong warm/green cast, then Auto Balance it.
    const QString clip = p.project.selectedClip();
    p.project.setColorValue(clip, "temperature", 0.8);
    p.project.setColorValue(clip, "tint", -0.6);
    QQuickItem* scope = p.ui->find("scope");
    ASSERT_TRUE(scope);
    auto cast = [&] {
        const QVariantMap s = scope->property("stats").toMap();
        const double r = s.value("meanR").toDouble(), g = s.value("meanG").toDouble(), b = s.value("meanB").toDouble();
        const double m = (r + g + b) / 3;
        return std::abs(r - m) + std::abs(g - m) + std::abs(b - m);
    };
    // Wait until the scopes show the final grade (both edits rendered): the same numbers twice.
    double last = -1;
    ASSERT_TRUE(test::waitFor([&] {
        test::settle(150);
        const double c = cast();
        const bool stable = c > 0.05 && std::abs(c - last) < 1e-9;
        last = c;
        return stable;
    }, 8000)) << "cast " << cast();
    const double before = cast();
    p.ui->click(p.center("autoBalance"));
    EXPECT_EQ(p.project.undoLabel(), "Auto balance");
    // The scopes measure the whole frame (a purple screen, an orange camera, the
    // background) and only the selected clip is balanced, so the frame keeps
    // part of its cast; exact recovery of a known cast is tested in
    // ControllerTest AutoBalanceRecoversAKnownCast.
    EXPECT_TRUE(test::waitFor([&] { return cast() < before * 0.85; }, 5000)) << before << " -> " << cast();
}

namespace {
/// The rendered plan's layer of a clip in the shown snapshot.
editor::VisualLayer shownLayer(ProjectController& project, const QString& clipId) {
    for (const auto& l : editor::buildRenderPlan(*project.snapshot(), Time::fromSeconds(1)).layers) {
        if (QString::fromStdString(l.clip.toString()) == clipId) return l;
    }
    return {};
}
}  // namespace

TEST(ColorPage, LooksPreviewOnHoverApplyWithAmountAndSpreadToAllClips) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    const QString clip = p.project.selectedClip();
    ASSERT_TRUE(test::waitFor([&] { return p.project.lookPreviewRevision() > 0; }, 5000)) << "look thumbnails";
    // Hovering a look previews it in the viewer without an edit.
    p.ui->move(p.center("look-dark-green"));
    ASSERT_TRUE(test::waitFor([&] { return shownLayer(p.project, clip).color.tint < -0.3; }, 3000));
    EXPECT_EQ(p.project.selection().value("look").toString(), "");
    EXPECT_FALSE(p.project.canUndo());
    // Clicking applies it.
    p.ui->click(p.center("look-dark-green"));
    EXPECT_EQ(p.project.selection().value("look").toString(), "dark-green");
    EXPECT_TRUE(p.project.canUndo());
    // Amount: drag the slider to the left.
    QQuickItem* slider = p.ui->find("lookAmount");
    ASSERT_TRUE(slider);
    p.ui->drag(QmlHarness::at(slider, {slider->width() - 6, slider->height() / 2}),
               QmlHarness::at(slider, {slider->width() * 0.4, slider->height() / 2}));
    EXPECT_LT(p.project.selection().value("lookAmount").toDouble(), 0.6);
    // Apply to all clips.
    p.ui->click(p.center("lookToAll"));
    for (const QString& id : p.project.gradableClips()) {
        p.project.selectClip(id);
        EXPECT_EQ(p.project.selection().value("look").toString(), "dark-green") << id.toStdString();
    }
}

TEST(ColorPage, AddedNodeGradesOnlyItsWindowShapedOnTheViewer) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    const QString clip = p.project.selectedClip();
    QTest::keyClick(&p.ui->window(), Qt::Key_S, Qt::AltModifier);  // ⌥S: add a serial node
    test::settle(80);
    ASSERT_EQ(p.project.selection().value("nodes").toList().size(), 1);
    QQuickItem* chip = p.ui->find("editingNode");
    ASSERT_TRUE(chip);
    // The wheels now grade the new node, not the clip's correction.
    const QPoint jog = p.center("master-gain");
    p.ui->drag(jog, jog + QPoint(100, 0));
    const QVariantMap node = p.project.selection().value("nodes").toList()[0].toMap();
    EXPECT_GT(node.value("gain").toMap().value("master").toDouble(), 0.2);
    EXPECT_NEAR(p.wheel("gain").value("master").toDouble(), 0.0, 1e-9);
    // Give it a circle window and shape it with the handles.
    p.ui->click(p.center("palette-window"));
    p.ui->click(p.center("window-circle"));
    auto window = [&] { return p.project.selection().value("nodes").toList()[0].toMap().value("window").toMap(); };
    ASSERT_EQ(window().value("shape").toString(), "circle");
    QQuickItem* right = p.ui->find("windowHandle-right");
    ASSERT_TRUE(right && right->isVisible());
    const double width0 = window().value("width").toDouble();
    const QPoint r0 = p.center("windowHandle-right");
    p.ui->drag(r0, r0 + QPoint(40, 0));
    EXPECT_GT(window().value("width").toDouble(), width0 + 0.02);
    // Drag inside the shape to move it.
    QQuickItem* shape = right->parentItem();
    const QPoint inside = QmlHarness::at(shape, {shape->width() / 2, shape->height() / 2});
    const double x0 = window().value("x").toDouble();
    p.ui->drag(inside, inside + QPoint(-50, 0));
    EXPECT_LT(window().value("x").toDouble(), x0 - 0.03);
    // The renderers get the node with its window.
    const editor::VisualLayer layer = shownLayer(p.project, clip);
    ASSERT_EQ(layer.nodes.size(), 1u);
    EXPECT_EQ(layer.nodes[0].window.shape, "circle");
    // Back to node 01 from the graph.
    p.ui->click(p.center("node-01"));
    const QPoint jog01 = p.center("master-gain");
    p.ui->drag(jog01, jog01 + QPoint(60, 0));
    EXPECT_GT(p.wheel("gain").value("master").toDouble(), 0.1);
}

TEST(ColorPage, ColorKeyIsPickedOnTheViewer) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    p.ui->click(p.center("palette-qualifier"));
    p.ui->click(p.center("addColorKey"));
    QQuickItem* pick = p.ui->find("pickArea");
    ASSERT_TRUE(pick && pick->isVisible()) << "picking starts with the new node";
    // Click the screen recording in the middle of the picture.
    p.ui->click(QmlHarness::at(pick, {pick->width() * 0.35, pick->height() * 0.4}));
    const QVariantList nodes = p.project.selection().value("nodes").toList();
    ASSERT_EQ(nodes.size(), 1);
    const QVariantMap q = nodes[0].toMap().value("qualifier").toMap();
    EXPECT_TRUE(q.value("enabled").toBool());
    EXPECT_EQ(p.project.undoLabel(), "Pick color");
    EXPECT_FALSE(pick->isVisible());  // one pick, then back to normal
    // The hue range bar is live: drag its middle to move the hue.
    QQuickItem* hue = p.ui->find("hueRange");
    ASSERT_TRUE(hue);
    const double hue0 = q.value("hue").toDouble();
    const double lowX = std::fmod(std::fmod(hue0 - q.value("hueWidth").toDouble(), 1.0) + 1.0, 1.0);
    const double highX = std::fmod(std::fmod(hue0 + q.value("hueWidth").toDouble(), 1.0) + 1.0, 1.0);
    if (lowX < highX) {  // not wrapping around the bar ends
        const QPoint mid = QmlHarness::at(hue, {(lowX + highX) / 2 * hue->width(), hue->height() / 2});
        p.ui->drag(mid, mid + QPoint(30, 0));
        EXPECT_NE(p.project.selection().value("nodes").toList()[0].toMap().value("qualifier").toMap().value("hue").toDouble(), hue0);
    }
}

TEST(ColorPage, WipeSideBySideAndBypassCompareTheGrade) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    p.ui->click(p.center("compareWipe"));
    EXPECT_EQ(p.playback->compareMode(), "wipe");
    QQuickItem* handle = p.ui->find("wipeHandle");
    ASSERT_TRUE(handle && handle->isVisible());
    const QPoint h = p.center("wipeHandle");
    p.ui->drag(h, h - QPoint(120, 0));
    EXPECT_LT(p.playback->compareSplit(), 0.4);
    QTest::keyClick(&p.ui->window(), Qt::Key_D, Qt::ShiftModifier);  // ⇧D: bypass all grades
    test::settle(40);
    EXPECT_EQ(p.playback->compareMode(), "bypass");
    QTest::keyClick(&p.ui->window(), Qt::Key_D, Qt::ShiftModifier);
    test::settle(40);
    EXPECT_EQ(p.playback->compareMode(), "wipe");
    p.ui->click(p.center("compareSide"));
    EXPECT_EQ(p.playback->compareMode(), "side");
    // Leaving the Color page shows the normal picture again.
    p.ui->find("colorPage")->setVisible(false);
    test::settle(40);
    EXPECT_EQ(p.playback->compareMode(), "off");
}

TEST(ColorPage, EffectsSwitchOnAndStillsKeepGrades) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    const QString clip = p.project.selectedClip();
    p.ui->click(p.center("fx-grain-switch"));
    EXPECT_TRUE(p.project.selection().value("grainOn").toBool());
    p.ui->click(p.center("fx-halation-switch"));
    EXPECT_TRUE(p.project.selection().value("halationOn").toBool());
    EXPECT_GT(shownLayer(p.project, clip).grain, 0.0);
    // Grab a still of a graded frame, change the grade, then take the grade back from the still.
    p.project.setColorValue(clip, "exposure", 0.4);
    p.ui->click(p.center("galleryTab-gallery"));
    p.ui->click(p.center("grabStill"));
    ASSERT_EQ(p.project.stills().size(), 1);
    p.project.setColorValue(clip, "exposure", -0.2);
    ASSERT_TRUE(test::waitFor([&] { return p.ui->find("still-0") != nullptr; }, 2000));
    QTest::mouseDClick(&p.ui->window(), Qt::LeftButton, {}, p.center("still-0"));
    test::settle(60);
    EXPECT_DOUBLE_EQ(p.project.selection().value("exposure").toDouble(), 0.4);
}

TEST(ColorPage, NoiseReductionAndSharpenSwitchOnWithDefaults) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    const QString clip = p.project.selectedClip();
    p.ui->click(p.center("fx-denoise-switch"));
    p.ui->click(p.center("fx-sharpen-switch"));
    const QVariantMap s = p.project.selection();
    EXPECT_TRUE(s.value("denoiseOn").toBool());
    EXPECT_TRUE(s.value("sharpenOn").toBool());
    const auto layer = shownLayer(p.project, clip);
    EXPECT_DOUBLE_EQ(layer.denoiseLuma, 0.3);
    EXPECT_DOUBLE_EQ(layer.denoiseChroma, 0.5);
    EXPECT_DOUBLE_EQ(layer.sharpen, 0.3);
    p.ui->click(p.center("fx-sharpen-switch"));  // and off again
    EXPECT_FALSE(p.project.selection().value("sharpenOn").toBool());
    EXPECT_EQ(shownLayer(p.project, clip).sharpen, 0.0);
}

TEST(ColorPage, CopyAndPasteGradeWithTheKeyboard) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    const QString first = p.project.selectedClip();
    p.project.setColorValue(first, "contrast", 0.35);
    QTest::keyClick(&p.ui->window(), Qt::Key_C, Qt::ControlModifier);
    test::settle(40);
    EXPECT_TRUE(p.project.hasCopiedGrade());
    // Another clip from the strip, then paste.
    QString other;
    for (const QString& id : p.project.gradableClips()) if (id != first) other = id;
    ASSERT_FALSE(other.isEmpty());
    p.project.selectClip(other);
    QTest::keyClick(&p.ui->window(), Qt::Key_V, Qt::ControlModifier);
    test::settle(40);
    EXPECT_DOUBLE_EQ(p.project.selection().value("contrast").toDouble(), 0.35);
}

// Screenshots of the Color page in typical states, for reviewing the design
// (runs only with LECTERN_DUMP_DIR set).
TEST(ColorPage, DumpScreens) {
    const char* dump = std::getenv("LECTERN_DUMP_DIR");
    if (!dump) GTEST_SKIP() << "set LECTERN_DUMP_DIR to write screenshots";
    Page p;
    ASSERT_TRUE(p.ui->ok());
    const QString dir = QString::fromLocal8Bit(dump);
    const QString clip = p.project.selectedClip();
    auto shot = [&](const char* name) {
        test::settle(500);
        p.ui->window().grabWindow().save(dir + "/" + name + ".png");
    };
    ASSERT_TRUE(test::waitFor([&] { return p.project.lookPreviewRevision() > 0; }, 5000));
    p.project.applyLook(clip, "teal-orange", 0.8);
    shot("color-looks");
    const QString node = p.project.addNode(clip, "circle");
    p.project.setNodeValue(clip, node, "exposure", 0.5);
    p.ui->click(p.center("node-n2"));
    p.ui->click(p.center("palette-window"));
    shot("color-window");
    p.ui->click(p.center("palette-qualifier"));
    shot("color-qualifier");
    p.ui->click(p.center("compareWipe"));
    p.ui->click(p.center("fx-grain-switch"));
    p.ui->click(p.center("fx-film-switch"));
    shot("color-wipe-effects");
}

TEST(ColorPage, HslCurvesAddDragSixVectorsAndPick) {
    Page p;
    ASSERT_TRUE(p.ui->ok());
    QQuickItem* palette = p.ui->find("curvesPalette");
    ASSERT_TRUE(palette);
    palette->setProperty("mode", QStringLiteral("hueVsSat"));
    test::settle(60);
    QQuickItem* editor = p.ui->find("hslCurveEditor");
    ASSERT_TRUE(editor && editor->isVisible());
    const double w = editor->width() - 20, h = editor->height() - 32;  // 10 px sides and top, 22 px for the axis band
    auto at = [&](double x, double y) { return QmlHarness::at(editor, {10 + x * w, 10 + (1 - y) * h}); };
    // Click the neutral line at green and drag down: greens lose saturation.
    p.ui->drag(at(0.333, 0.5), at(0.333, 0.15));
    if (const char* dump = std::getenv("LECTERN_DUMP_DIR")) p.ui->window().grabWindow().save(QString::fromLocal8Bit(dump) + "/hsl.png");
    QVariantList pts = p.project.selection().value("hueVsSat").toList();
    ASSERT_EQ(pts.size(), 3) << "a point and two anchors";
    double lowest = 1;
    for (const QVariant& v : pts) lowest = std::min(lowest, v.toMap().value("y").toDouble());
    EXPECT_LT(lowest, 0.25);
    // Six vectors adds points at the six hues it does not have yet.
    p.ui->click(p.center("sixVectors"));
    pts = p.project.selection().value("hueVsSat").toList();
    EXPECT_GE(pts.size(), 8);
    // Pick: a click on the picture adds a point at its hue.
    p.ui->click(p.center("resetHsl"));
    EXPECT_TRUE(p.project.selection().value("hueVsSat").toList().isEmpty());
    p.ui->click(p.center("pickCurveColor"));
    QQuickItem* pick = p.ui->find("pickArea");
    ASSERT_TRUE(pick && pick->isVisible());
    p.ui->click(QmlHarness::at(pick, {pick->width() * 0.35, pick->height() * 0.4}));
    EXPECT_EQ(p.project.selection().value("hueVsSat").toList().size(), 3);
    EXPECT_FALSE(pick->isVisible());
    // Back to the custom curves.
    palette->setProperty("mode", QStringLiteral("custom"));
    test::settle(30);
    EXPECT_TRUE(p.ui->find("curveEditor")->isVisible());
}
