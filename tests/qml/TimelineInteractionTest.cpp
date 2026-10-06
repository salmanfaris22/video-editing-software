// The timeline with real mouse input: scrub on the ruler and by grabbing the
// playhead (snapping, auto-scroll at the edges), move and trim clips without
// the view stealing the drag, drag a clip onto another layer, pick layers in
// the headers, and scroll/zoom with the wheel.

#include "editor/EditorFixture.h"
#include "project/ProjectStore.h"
#include "qml/QmlHarness.h"
#include "ui/PlaybackController.h"
#include "ui/ProjectController.h"

#include <QWheelEvent>

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::ui;
using test::QmlHarness;

namespace {

const QByteArray kTimeline = R"(
import QtQuick
import Lectern.UI
Item {
    required property ProjectController project
    required property PlaybackController playback
    TimelinePanel { objectName: "timeline"; anchors.fill: parent; project: parent.project; playback: parent.playback }
}
)";

struct Timeline {
    test::EditorFixture fixture{test::EditorFixture::Options{.seconds = 12.0}};
    ProjectController project;
    std::unique_ptr<PlaybackController> playback;
    std::unique_ptr<QmlHarness> ui;
    QQuickItem* panel = nullptr;
    QQuickItem* lanes = nullptr;
    QQuickItem* ruler = nullptr;

    Timeline() {
        fixture.addText("Hello", 2.0, 2.0, "title");
        EXPECT_TRUE(project::ProjectStore::save(fixture.dir.path(), fixture.project));
        project.open(QString::fromStdString(fixture.dir.path().string()));
        EXPECT_TRUE(test::waitFor([this] { return project.loaded(); }));
        playback = std::make_unique<PlaybackController>(&project, /*silent=*/true);
        ui = std::make_unique<QmlHarness>(kTimeline,
                                          QVariantMap{{"project", QVariant::fromValue(&project)},
                                                      {"playback", QVariant::fromValue(playback.get())}},
                                          QSize(1100, 420));
        panel = ui->find("timeline");
        lanes = ui->find("timelineLanes");
        ruler = ui->find("timelineRuler");
        EXPECT_TRUE(panel && lanes && ruler);
        test::settle(120);  // fit-to-width runs once laid out
    }
    double pps() const { return panel->property("pxPerSecond").toDouble(); }
    double contentX() const { return lanes->property("contentX").toDouble(); }
    /// Window point on the ruler at timeline time `t`.
    QPoint onRuler(double t) const { return QmlHarness::at(ruler, {t * pps(), 10}); }
    QVariantMap track(int index) const { return project.tracks().value(index).toMap(); }
    int trackIndex(const QString& name) const {
        for (int i = 0; i < project.tracks().size(); ++i)
            if (track(i).value("name").toString() == name) return i;
        return -1;
    }
    QVariantMap clip(const QString& trackName) const {
        return track(trackIndex(trackName)).value("clips").toList().value(0).toMap();
    }
    QQuickItem* clipItem(const QString& id) const { return ui->find(("clip-" + id).toUtf8().constData()); }
    QPoint clipCenter(const QString& id) const {
        QQuickItem* c = clipItem(id);
        EXPECT_TRUE(c) << "no clip item " << id.toStdString();
        if (!c) return {};
        return QmlHarness::at(c, {c->width() / 2, c->height() / 2});
    }
    void wheel(QPoint at, QPoint pixelDelta, Qt::KeyboardModifiers mods = {}) {
        QWheelEvent e(QPointF(at), QPointF(ui->window().mapToGlobal(at)), pixelDelta, pixelDelta,
                      Qt::NoButton, mods, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&ui->window(), &e);
        test::settle(20);
    }
};

}  // namespace

TEST(TimelineInteraction, DraggingOnTheRulerScrubsFrameAccurately) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    const double fps = t.project.frameRate();
    t.ui->press(t.onRuler(1.3));
    EXPECT_TRUE(t.panel->property("scrubbing").toBool());
    EXPECT_NEAR(t.playback->position(), 1.3, 0.6 / fps);
    // Every move seeks right away (the playhead follows the hand).
    for (double s = 1.4; s <= 6.6; s += 0.4) {
        t.ui->move(t.onRuler(s), Qt::ControlModifier);  // ⌘: no snapping
        test::settle(4);
        EXPECT_NEAR(t.playback->position(), s, 1.0 / fps + 1.0 / t.pps()) << "at " << s;
    }
    t.ui->release(t.onRuler(6.6));
    test::settle(30);
    EXPECT_FALSE(t.panel->property("scrubbing").toBool());
    // Lands on a frame boundary.
    const double frames = t.playback->position() * fps;
    EXPECT_NEAR(frames, std::round(frames), 1e-6);
}

TEST(TimelineInteraction, ScrubbingSnapsToClipEdgesUnlessCommandIsHeld) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    // The text clip starts at 2 s: 4 px away snaps onto it.
    const QPoint near = t.onRuler(2.0) + QPoint(4, 0);
    t.ui->drag(t.onRuler(5.0), near);
    EXPECT_NEAR(t.playback->position(), 2.0, 1e-6);
    t.ui->drag(t.onRuler(5.0), near, Qt::ControlModifier);
    EXPECT_GT(t.playback->position(), 2.0 + 1.0 / t.project.frameRate() / 2);
}

TEST(TimelineInteraction, ThePlayheadCanBeGrabbedInTheLanes) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    t.playback->seek(6.0);
    test::settle(30);
    QQuickItem* grab = t.ui->find("playheadGrab");
    ASSERT_TRUE(grab);
    const QPoint from = QmlHarness::at(grab, {grab->width() / 2, 30});
    const QPoint to = from - QPoint(int(1.5 * t.pps()), 0);
    t.ui->drag(from, to, Qt::ControlModifier);
    EXPECT_NEAR(t.playback->position(), 4.5, 2.0 / t.pps() + 1.0 / t.project.frameRate());
    EXPECT_TRUE(t.project.selectedClips().isEmpty());  // grabbing the playhead selects nothing
}

TEST(TimelineInteraction, ScrubbingPausesPlaybackAndResumesAfter) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    t.playback->play();
    ASSERT_TRUE(test::waitFor([&] { return t.playback->playing(); }, 3000));
    t.ui->press(t.onRuler(4.0));
    ASSERT_TRUE(test::waitFor([&] { return !t.playback->playing(); }, 3000));
    t.ui->move(t.onRuler(5.0));
    t.ui->release(t.onRuler(5.0));
    EXPECT_TRUE(test::waitFor([&] { return t.playback->playing(); }, 3000));
    t.playback->pause();
}

TEST(TimelineInteraction, DraggingAClipMovesItAndIsNotStolenByScrolling) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    const QString text = t.clip("Text").value("id").toString();
    const double x0 = t.contentX();
    const QPoint from = t.clipCenter(text);
    t.ui->drag(from, from + QPoint(int(3.0 * t.pps()), 0), Qt::ControlModifier);
    EXPECT_NEAR(t.clip("Text").value("start").toDouble(), 5.0, 2.0 / t.pps());
    EXPECT_EQ(t.contentX(), x0);  // the view did not pan
    EXPECT_EQ(t.project.selectedClip(), text);

    // Without ⌘ the start snaps onto the playhead when dropped close to it.
    t.playback->seek(7.0);
    test::settle(30);
    const QPoint grab = t.clipCenter(text);
    t.ui->drag(grab, grab + QPoint(int(2.0 * t.pps()) + 3, 0));
    EXPECT_NEAR(t.clip("Text").value("start").toDouble(), 7.0, 1e-6);
}

TEST(TimelineInteraction, TrimmingAnEdgeSnapsToThePlayhead) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    t.playback->seek(5.0);
    test::settle(30);
    const QString text = t.clip("Text").value("id").toString();
    QQuickItem* c = t.clipItem(text);
    ASSERT_TRUE(c);
    const QPoint end = QmlHarness::at(c, {c->width() - 2, c->height() / 2});
    t.ui->drag(end, end + QPoint(int(1.0 * t.pps()) - 4, 0));  // 4 px short of the playhead
    EXPECT_NEAR(t.clip("Text").value("start").toDouble(), 2.0, 1e-6);
    EXPECT_NEAR(t.clip("Text").value("duration").toDouble(), 3.0, 1e-6);
}

TEST(TimelineInteraction, AClipCanBeDraggedOntoAnotherLayerOfItsKind) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    const QString layer = t.project.addTrack("overlay");  // shown on top, above Text
    test::settle(60);
    ASSERT_EQ(t.track(0).value("id").toString(), layer);
    const QString text = t.clip("Text").value("id").toString();
    const double height = t.panel->property("trackHeight").toDouble();
    const QPoint from = t.clipCenter(text);
    t.ui->drag(from, from - QPoint(0, int(height)), Qt::ControlModifier);
    const QVariantList moved = t.track(0).value("clips").toList();
    ASSERT_EQ(moved.size(), 1);
    EXPECT_EQ(moved.value(0).toMap().value("id").toString(), text);
    EXPECT_NEAR(moved.value(0).toMap().value("start").toDouble(), 2.0, 2.0 / t.pps());
    EXPECT_EQ(t.project.selectedTrack(), layer);

    // Onto an audio lane: not allowed, the clip stays on its layer.
    const QPoint again = t.clipCenter(text);
    const int audio = t.trackIndex("Microphone");
    t.ui->drag(again, again + QPoint(0, int(height * audio)), Qt::ControlModifier);
    EXPECT_EQ(t.track(0).value("clips").toList().size(), 1);
    EXPECT_EQ(t.panel->property("dropLane").toInt(), -1);  // drop highlight cleared
}

TEST(TimelineInteraction, HeadersSelectRenameAndReorderLayers) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    const QString cameraTrack = t.track(t.trackIndex("Camera")).value("id").toString();
    QQuickItem* header = t.ui->find(("trackHeader-" + cameraTrack).toUtf8().constData());
    ASSERT_TRUE(header);
    t.ui->click(QmlHarness::at(header, {80, header->height() / 2}));
    EXPECT_EQ(t.project.selectedTrack(), cameraTrack);

    // Clicking a clip selects its layer.
    const QString text = t.clip("Text").value("id").toString();
    t.ui->click(t.clipCenter(text));
    EXPECT_EQ(t.project.selectedTrack(), t.track(t.trackIndex("Text")).value("id").toString());

    // Double-click the name to rename it.
    const QString textTrack = t.track(t.trackIndex("Text")).value("id").toString();
    QQuickItem* textHeader = t.ui->find(("trackHeader-" + textTrack).toUtf8().constData());
    ASSERT_TRUE(textHeader);
    const QPoint name = QmlHarness::at(textHeader, {80, textHeader->height() / 2});
    QTest::mouseDClick(&t.ui->window(), Qt::LeftButton, {}, name);
    test::settle(40);
    QQuickItem* field = textHeader->findChild<QQuickItem*>("layerNameField");
    ASSERT_TRUE(field);
    EXPECT_TRUE(field->hasActiveFocus());
    QTest::keyClick(&t.ui->window(), Qt::Key_A, Qt::ControlModifier);
    t.ui->type("Title");
    QTest::keyClick(&t.ui->window(), Qt::Key_Return);
    test::settle(60);
    EXPECT_EQ(t.trackIndex("Text"), -1);
    ASSERT_GE(t.trackIndex("Title"), 0);

    // The up/down buttons move the selected layer in the stacking order.
    const QString layer = t.project.addTrack("overlay");
    test::settle(60);
    ASSERT_EQ(t.track(0).value("id").toString(), layer);
    QQuickItem* down = t.ui->find("layerDownButton");
    QQuickItem* up = t.ui->find("layerUpButton");
    ASSERT_TRUE(down && up);
    EXPECT_FALSE(up->isEnabled());
    t.ui->click(QmlHarness::at(down, {down->width() / 2, down->height() / 2}));
    test::settle(40);
    EXPECT_EQ(t.track(1).value("id").toString(), layer);
    EXPECT_EQ(t.project.selectedTrack(), layer);
    t.ui->click(QmlHarness::at(up, {up->width() / 2, up->height() / 2}));
    test::settle(40);
    EXPECT_EQ(t.track(0).value("id").toString(), layer);
}

TEST(TimelineInteraction, AddLayerMenuAddsTextAtThePlayhead) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    t.playback->seek(5.0);
    QQuickItem* button = t.ui->find("addLayerButton");
    ASSERT_TRUE(button);
    t.ui->click(QmlHarness::at(button, {button->width() / 2, button->height() / 2}));
    QObject* menu = t.panel->findChild<QObject*>("addLayerMenu");
    ASSERT_TRUE(menu);
    EXPECT_TRUE(test::waitFor([&] { return menu->property("visible").toBool(); }, 2000));
    QMetaObject::invokeMethod(menu, "close");
    const int before = static_cast<int>(t.project.tracks().size());
    QMetaObject::invokeMethod(t.panel, "addLayer", Q_ARG(QVariant, QStringLiteral("text")));
    test::settle(40);
    // The Text layer is busy at 5 s? No — its clip ends at 4 s, so it is reused.
    EXPECT_EQ(t.project.tracks().size(), before);
    const QVariantMap sel = t.project.selection();
    EXPECT_EQ(sel.value("role").toString(), "text");
    EXPECT_NEAR(sel.value("start").toDouble(), 5.0, 1e-6);
    QMetaObject::invokeMethod(t.panel, "addLayer", Q_ARG(QVariant, QStringLiteral("empty-audio")));
    test::settle(40);
    EXPECT_EQ(t.project.tracks().size(), before + 1);
    EXPECT_EQ(t.project.tracks().last().toMap().value("kind").toString(), "audio");
}

TEST(TimelineInteraction, WheelScrollsAndCommandWheelZoomsAroundThePointer) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    t.panel->setProperty("pxPerSecond", 200.0);  // much wider than the view
    test::settle(40);
    const QPoint mid = QmlHarness::at(t.lanes, {t.lanes->width() / 2, 20});
    t.wheel(mid, QPoint(0, -120));  // plain vertical wheel scrolls through time when all layers fit
    EXPECT_NEAR(t.contentX(), 120, 1);
    t.wheel(mid, QPoint(-60, 0));
    EXPECT_NEAR(t.contentX(), 180, 1);

    const double timeUnder = (t.contentX() + t.lanes->width() / 2) / t.pps();
    t.wheel(mid, QPoint(0, 60), Qt::ControlModifier);
    EXPECT_GT(t.pps(), 200.0);
    EXPECT_NEAR((t.contentX() + t.lanes->width() / 2) / t.pps(), timeUnder, 0.01);  // the time under the pointer stays
}

TEST(TimelineInteraction, ScrubbingAtTheEdgeAutoScrolls) {
    Timeline t;
    ASSERT_TRUE(t.ui->ok());
    t.panel->setProperty("pxPerSecond", 200.0);
    test::settle(40);
    const QPoint edge = QmlHarness::at(t.lanes, {t.lanes->width() - 6, 20});
    const QPoint start = QmlHarness::at(t.lanes, {t.lanes->width() / 2, 20});
    t.ui->press(QPoint(start.x(), t.onRuler(0).y()));
    t.ui->move(QPoint(edge.x(), t.onRuler(0).y()));
    const double before = t.playback->position();
    test::settle(400);
    EXPECT_GT(t.contentX(), 50);
    EXPECT_GT(t.playback->position(), before + 0.2);  // the playhead keeps following the scroll
    t.ui->release(QPoint(edge.x(), t.onRuler(0).y()));
    test::settle(100);
    const double x = t.contentX();
    test::settle(100);
    EXPECT_EQ(t.contentX(), x);  // stops on release
}
