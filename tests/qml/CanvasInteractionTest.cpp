// The preview's canvas tools with real mouse and keyboard input: pick a
// layer, drag it (snapping), resize it by a handle, cancel with Esc, edit
// text in place.

#include "editor/EditorFixture.h"
#include "editor/RenderPlan.h"
#include "project/ProjectStore.h"
#include "qml/QmlHarness.h"
#include "ui/PlaybackController.h"
#include "ui/ProjectController.h"

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::ui;
using test::QmlHarness;

namespace {

const QByteArray kPreview = R"(
import QtQuick
import Lectern.UI
Item {
    required property ProjectController project
    required property PlaybackController playback
    PreviewPane { anchors.fill: parent; project: parent.project; playback: parent.playback }
}
)";

struct Editor {
    test::EditorFixture fixture;
    ProjectController project;
    std::unique_ptr<PlaybackController> playback;
    std::unique_ptr<QmlHarness> ui;
    QQuickItem* canvas = nullptr;

    Editor() {
        EXPECT_TRUE(project::ProjectStore::save(fixture.dir.path(), fixture.project));
        project.open(QString::fromStdString(fixture.dir.path().string()));
        EXPECT_TRUE(test::waitFor([this] { return project.loaded(); }));
        playback = std::make_unique<PlaybackController>(&project, /*silent=*/true);
        playback->seek(1.0);
        ui = std::make_unique<QmlHarness>(kPreview, QVariantMap{{"project", QVariant::fromValue(&project)},
                                                                {"playback", QVariant::fromValue(playback.get())}});
        canvas = ui->find("canvasOverlay");
        EXPECT_TRUE(canvas);
    }
    /// Window point of a canvas fraction.
    QPoint at(double fx, double fy) const { return QmlHarness::at(canvas, {fx * canvas->width(), fy * canvas->height()}); }
    QVariantMap box(const QString& clip) { return project.layerBox(clip, 1.0); }
    QString clipId(const QString& trackName) const {
        for (const QVariant& t : project.tracks()) {
            const QVariantMap m = t.toMap();
            if (m.value("name").toString() == trackName) return m.value("clips").toList().value(0).toMap().value("id").toString();
        }
        return {};
    }
    double px(double fraction) const { return fraction * canvas->width(); }
};

double cx(const QVariantMap& b) { return b.value("x").toDouble() + b.value("w").toDouble() / 2; }
double cy(const QVariantMap& b) { return b.value("y").toDouble() + b.value("h").toDouble() / 2; }

}  // namespace

TEST(CanvasInteraction, ClickSelectsTheLayerUnderThePointer) {
    Editor e;
    ASSERT_TRUE(e.ui->ok());
    const QString camera = e.clipId("Camera");
    const QString screen = e.clipId("Screen");
    const QVariantMap cam = e.box(camera);
    e.ui->click(e.at(cx(cam), cy(cam)));
    EXPECT_EQ(e.project.selectedClip(), camera);
    e.ui->click(e.at(0.25, 0.3));
    EXPECT_EQ(e.project.selectedClip(), screen);
    EXPECT_FALSE(e.playback->playing());  // clicking layers does not start playback
}

TEST(CanvasInteraction, DragMovesTheCameraAndSnapsToTheCenter) {
    Editor e;
    ASSERT_TRUE(e.ui->ok());
    const QString camera = e.clipId("Camera");
    const QVariantMap before = e.box(camera);
    // Drag by about a third of the canvas to the left and up: one undo step.
    const QPoint from = e.at(cx(before), cy(before));
    const QPoint to = from + QPoint(-int(e.px(0.3)), -int(e.canvas->height() * 0.25));
    e.ui->drag(from, to);
    const QVariantMap after = e.box(camera);
    const double movedX = cx(after) - cx(before);
    EXPECT_NEAR(movedX, -0.3, 2.0 / e.canvas->width());
    EXPECT_NEAR(after.value("w").toDouble(), before.value("w").toDouble(), 1e-9);  // a move keeps the size
    EXPECT_TRUE(e.project.layoutCustomized(1.0));
    e.project.undo();
    EXPECT_NEAR(cx(e.box(camera)), cx(before), 1e-9);  // the whole drag was one step
    e.project.redo();

    // Dropped a few pixels off the canvas center: it snaps exactly onto it.
    const QVariantMap now = e.box(camera);
    const QPoint grab = e.at(cx(now), cy(now));
    const QPoint target = e.at(0.5, 0.5) + QPoint(3, -2);
    e.ui->drag(grab, target);
    EXPECT_NEAR(cx(e.box(camera)), 0.5, 1e-6);
    EXPECT_NEAR(cy(e.box(camera)), 0.5, 1e-6);

    // With ⌘ held it lands exactly where it is dropped.
    const QPoint grab2 = e.at(0.5, 0.5);
    e.ui->drag(grab2, grab2 + QPoint(3, 0), Qt::ControlModifier);
    EXPECT_NEAR(cx(e.box(camera)) * e.canvas->width(), 0.5 * e.canvas->width() + 3, 1.0);
}

TEST(CanvasInteraction, CornerHandlesResizeKeepingProportionsAndEdgesReshapeTheCamera) {
    Editor e;
    ASSERT_TRUE(e.ui->ok());
    const QString camera = e.clipId("Camera");
    const QVariantMap before = e.box(camera);
    e.ui->click(e.at(cx(before), cy(before)));  // select
    ASSERT_EQ(e.project.selectedClip(), camera);
    const double x0 = before.value("x").toDouble();
    const double y0 = before.value("y").toDouble();
    const double w0 = before.value("w").toDouble();
    const double h0 = before.value("h").toDouble();
    // Drag the top-left corner outwards along the diagonal: twice as large,
    // anchored at the bottom-right corner.
    e.ui->drag(e.at(x0, y0), e.at(x0 - w0, y0 - h0));
    const QVariantMap big = e.box(camera);
    EXPECT_NEAR(big.value("w").toDouble(), 2 * w0, 3.0 / e.canvas->width());
    EXPECT_NEAR(big.value("h").toDouble(), 2 * h0, 3.0 / e.canvas->height());
    EXPECT_NEAR(big.value("x").toDouble() + big.value("w").toDouble(), x0 + w0, 2.0 / e.canvas->width());
    EXPECT_NEAR(big.value("y").toDouble() + big.value("h").toDouble(), y0 + h0, 2.0 / e.canvas->height());

    // The right edge handle changes only the width (the camera is cropped to fill).
    const QVariantMap b = e.box(camera);
    const double right = b.value("x").toDouble() + b.value("w").toDouble();
    const QPoint edge = e.at(right, cy(b));
    e.ui->drag(edge, edge + QPoint(-int(e.px(b.value("w").toDouble() * 0.25)), 0));
    const QVariantMap narrow = e.box(camera);
    EXPECT_NEAR(narrow.value("w").toDouble(), b.value("w").toDouble() * 0.75, 3.0 / e.canvas->width());
    EXPECT_NEAR(narrow.value("h").toDouble(), b.value("h").toDouble(), 1e-6);
}

TEST(CanvasInteraction, EscapeDuringADragPutsTheLayerBack) {
    Editor e;
    ASSERT_TRUE(e.ui->ok());
    const QString camera = e.clipId("Camera");
    const QVariantMap before = e.box(camera);
    const QPoint from = e.at(cx(before), cy(before));
    e.ui->press(from);
    for (int i = 1; i <= 8; ++i) {
        e.ui->move(from + QPoint(-20 * i, -10 * i));
        test::settle(10);
    }
    QTest::keyClick(&e.ui->window(), Qt::Key_Escape);
    test::settle(30);
    e.ui->release(from + QPoint(-160, -80));
    test::settle(50);
    EXPECT_NEAR(cx(e.box(camera)), cx(before), 1e-9);
    EXPECT_FALSE(e.project.canUndo());
    // The preview shows the document again (no leftover of the cancelled drag).
    const editor::RenderPlan shown = editor::buildRenderPlan(*e.project.snapshot(), Time::fromSeconds(1));
    for (const auto& l : shown.layers) {
        if (l.role == "camera") EXPECT_NEAR(l.box.x + l.box.w / 2, cx(before), 1e-9);
    }
}

TEST(CanvasInteraction, DoubleClickEditsTextInPlace) {
    Editor e;
    ASSERT_TRUE(e.ui->ok());
    e.project.addText("Old title", 0.0, 3.0, "callout");
    e.project.setTextValue(e.project.selectedClip(), "animationIn", "none");
    const QString text = e.project.selectedClip();
    const QVariantMap b = e.box(text);
    const QPoint p = e.at(cx(b), cy(b));
    QTest::mouseDClick(&e.ui->window(), Qt::LeftButton, {}, p);
    test::settle(80);
    e.ui->type("New title");  // replaces the selected text
    QTest::keyClick(&e.ui->window(), Qt::Key_Return);
    test::settle(50);
    EXPECT_EQ(e.project.selection().value("text").toString(), "New title");

    // Esc cancels an edit.
    QTest::mouseDClick(&e.ui->window(), Qt::LeftButton, {}, p);
    test::settle(80);
    e.ui->type("Discard me");
    QTest::keyClick(&e.ui->window(), Qt::Key_Escape);
    test::settle(50);
    EXPECT_EQ(e.project.selection().value("text").toString(), "New title");
}
