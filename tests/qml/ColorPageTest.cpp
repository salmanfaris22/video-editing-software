// The Color page with real mouse input: wheels (puck and master jog),
// scrubby number fields, double-click reset, scope modes and undo.

#include "editor/EditorFixture.h"
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
    ASSERT_TRUE(test::waitFor([&] { return cast() > 0.05; }, 5000)) << "cast " << cast();
    const double before = cast();
    p.ui->click(p.center("autoBalance"));
    // The test picture is strongly colored by design, so Temp/Tint reach their limits: at least halved.
    EXPECT_TRUE(test::waitFor([&] { return cast() < before * 0.5; }, 5000)) << before << " -> " << cast();
    EXPECT_TRUE(p.project.canUndo());
}
