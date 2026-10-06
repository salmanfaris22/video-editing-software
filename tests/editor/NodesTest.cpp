// Nodes (secondary corrections): power windows, the HSL qualifier, person /
// background subjects, and how the CPU renderer applies them.

#include "editor/ColorGrading.h"
#include "editor/Looks.h"
#include "editor/RenderPlan.h"
#include "timeline/TimelineJson.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

#include <cmath>

using namespace lectern;
using namespace lectern::editor;
using Window = timeline::ColorAdjustments::Window;
using Qualifier = timeline::ColorAdjustments::Qualifier;

namespace {

Window circle(double x, double y, double w, double h, double softness = 0.0) {
    Window win;
    win.shape = "circle";
    win.x = x;
    win.y = y;
    win.width = w;
    win.height = h;
    win.softness = softness;
    return win;
}

/// Identity map for an image of `w` × `h` covering the whole source.
SourceMap fullMap(int w, int h) {
    SourceMap m;
    m.du = 1.0 / w;
    m.dv = 1.0 / h;
    m.aspect = static_cast<double>(w) / h;
    return m;
}

}  // namespace

TEST(Windows, CircleIsRoundOnAWidePicture) {
    const Window w = circle(0.5, 0.5, 0.25, 0.25 * 16.0 / 9.0);  // a round shape on 16:9 (height in source heights)
    const double aspect = 16.0 / 9.0;
    EXPECT_NEAR(windowMatte(w, 0.5, 0.5, aspect), 1.0, 1e-9);
    EXPECT_NEAR(windowMatte(w, 0.95, 0.5, aspect), 0.0, 1e-9);
    // The edge is equally far from the center horizontally and vertically, in pixels.
    const double radiusPx = 0.125 * 1920;  // half the width, in pixels of a 1920-wide source
    EXPECT_NEAR(windowMatte(w, 0.5 + (radiusPx - 3) / 1920, 0.5, aspect), 1.0, 1e-6);
    EXPECT_NEAR(windowMatte(w, 0.5, 0.5 + (radiusPx - 3) / 1080, aspect), 1.0, 1e-6);
    EXPECT_NEAR(windowMatte(w, 0.5 + (radiusPx + 3) / 1920, 0.5, aspect), 0.0, 1e-6);
    EXPECT_NEAR(windowMatte(w, 0.5, 0.5 + (radiusPx + 3) / 1080, aspect), 0.0, 1e-6);
}

TEST(Windows, SoftnessFeathersTheEdgeAndInvertFlips) {
    Window w = circle(0.5, 0.5, 0.5, 0.5, 0.4);
    const double edge = windowMatte(w, 0.75, 0.5, 1.0);  // exactly on the edge
    EXPECT_NEAR(edge, 0.5, 1e-6);
    EXPECT_GT(windowMatte(w, 0.7, 0.5, 1.0), 0.5);
    EXPECT_LT(windowMatte(w, 0.8, 0.5, 1.0), 0.5);
    w.invert = true;
    EXPECT_NEAR(windowMatte(w, 0.5, 0.5, 1.0), 0.0, 1e-9);
    EXPECT_NEAR(windowMatte(w, 0.0, 0.0, 1.0), 1.0, 1e-9);
}

TEST(Windows, RectangleRotatesAndGradientFades) {
    Window r;
    r.shape = "rectangle";
    r.width = 0.8;
    r.height = 0.2;
    r.softness = 0;
    EXPECT_NEAR(windowMatte(r, 0.85, 0.5, 1.0), 1.0, 1e-9);  // inside the long side
    EXPECT_NEAR(windowMatte(r, 0.5, 0.85, 1.0), 0.0, 1e-9);
    r.rotation = 90;  // now tall
    EXPECT_NEAR(windowMatte(r, 0.85, 0.5, 1.0), 0.0, 1e-9);
    EXPECT_NEAR(windowMatte(r, 0.5, 0.85, 1.0), 1.0, 1e-9);

    Window g;
    g.shape = "gradient";
    g.height = 1.0;  // fades over the whole height
    EXPECT_NEAR(windowMatte(g, 0.5, 0.0, 1.0), 1.0, 1e-9);  // full at the top
    EXPECT_NEAR(windowMatte(g, 0.5, 0.5, 1.0), 0.5, 1e-9);
    EXPECT_NEAR(windowMatte(g, 0.5, 1.0, 1.0), 0.0, 1e-9);  // none at the bottom
    EXPECT_NEAR(windowMatte(Window{}, 0.1, 0.9, 1.0), 1.0, 1e-9);  // no window: everywhere
}

TEST(Qualifier, AxesAreHueSaturationLuma) {
    auto near3 = [](std::array<double, 3> a, std::array<double, 3> b) {
        for (int i = 0; i < 3; ++i) EXPECT_NEAR(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)], 1e-9);
    };
    near3(qualifierAxes(1, 0, 0), {0.0, 1.0, 0.2126});
    near3(qualifierAxes(0, 1, 0), {1.0 / 3.0, 1.0, 0.7152});
    near3(qualifierAxes(0, 0, 1), {2.0 / 3.0, 1.0, 0.0722});
    near3(qualifierAxes(0.5, 0.5, 0.5), {0.0, 0.0, 0.5});
    EXPECT_NEAR(qualifierAxes(1, 0, 0.5)[0], 11.0 / 12.0, 1e-9);  // pink-red wraps below 1
}

TEST(Qualifier, PicksOneColorAndItsShading) {
    // A blue pen on paper next to skin: picking the pen selects its shades only.
    const Qualifier q = qualifierAround(0.15, 0.3, 0.75);
    EXPECT_TRUE(q.enabled);
    EXPECT_GT(qualifierMatte(q, 0.15, 0.3, 0.75), 0.99);
    EXPECT_GT(qualifierMatte(q, 0.1, 0.22, 0.6), 0.9);    // the pen's shadow side
    EXPECT_LT(qualifierMatte(q, 0.82, 0.58, 0.47), 0.01);  // skin
    EXPECT_LT(qualifierMatte(q, 0.9, 0.9, 0.88), 0.01);    // paper
    EXPECT_LT(qualifierMatte(q, 0.25, 0.45, 0.18), 0.01);  // foliage
    Qualifier inverted = q;
    inverted.invert = true;
    EXPECT_LT(qualifierMatte(inverted, 0.15, 0.3, 0.75), 0.01);

    // Picking a gray keys on brightness, whatever the hue.
    const Qualifier gray = qualifierAround(0.2, 0.2, 0.2);
    EXPECT_GT(qualifierMatte(gray, 0.2, 0.2, 0.2), 0.99);
    EXPECT_LT(qualifierMatte(gray, 0.95, 0.95, 0.95), 0.01);
    EXPECT_EQ(qualifierMatte(Qualifier{}, 0.3, 0.6, 0.1), 1.0);  // disabled: everything
}

TEST(Nodes, SubjectAndInvertCombine) {
    NodeParams n;
    n.subject = 1;
    EXPECT_DOUBLE_EQ(nodeMatte(n, 0.5, 0.5, 1, 0.5, 0.5, 0.5, 0.8), 0.8);
    n.subject = 2;
    EXPECT_NEAR(nodeMatte(n, 0.5, 0.5, 1, 0.5, 0.5, 0.5, 0.8), 0.2, 1e-12);
    EXPECT_DOUBLE_EQ(nodeMatte(n, 0.5, 0.5, 1, 0.5, 0.5, 0.5, -1.0), 1.0);  // no mask: no limit
    n.window = circle(0.5, 0.5, 0.2, 0.2);
    EXPECT_NEAR(nodeMatte(n, 0.9, 0.9, 1, 0.5, 0.5, 0.5, 0.0), 0.0, 1e-12);
    n.invert = true;
    EXPECT_NEAR(nodeMatte(n, 0.9, 0.9, 1, 0.5, 0.5, 0.5, 0.0), 1.0, 1e-12);
}

TEST(Nodes, CpuAppliesOnlyInsideTheSelection) {
    // Left half red, right half blue; a node keyed on blue turns it gray.
    QImage img(64, 32, QImage::Format_RGB32);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) img.setPixel(x, y, x < 32 ? qRgb(200, 40, 30) : qRgb(30, 60, 200));
    NodeParams blue;
    blue.qualifier = qualifierAround(30 / 255.0, 60 / 255.0, 200 / 255.0);
    blue.grade.saturation = -1.0;
    QImage out = img;
    applyNodes(out, {blue}, fullMap(64, 32));
    const QColor left = out.pixelColor(10, 10);
    const QColor right = out.pixelColor(50, 10);
    EXPECT_EQ(left, QColor(200, 40, 30));
    EXPECT_NEAR(right.red(), right.blue(), 1);
    EXPECT_NEAR(right.green(), right.blue(), 1);

    // A circle window in the middle brightens only the middle.
    NodeParams spot;
    spot.window = circle(0.5, 0.5, 0.25, 0.5);
    spot.grade.exposure = 1.0;
    spot.grade = paramsOf([] {
        timeline::ColorAdjustments::Grade g;
        g.exposure = 1.0;
        return g;
    }());
    out = img;
    applyNodes(out, {spot}, fullMap(64, 32));
    EXPECT_GT(out.pixelColor(30, 16).red(), 200);         // inside, red side
    EXPECT_EQ(out.pixelColor(2, 2), QColor(200, 40, 30));  // corner untouched

    // Mirrored layers keep the window on the same source point.
    NodeParams left25;
    left25.window = circle(0.25, 0.5, 0.2, 0.4);
    left25.grade = spot.grade;
    SourceMap mirrored = fullMap(64, 32);
    mirrored.mirror = true;
    out = img;
    applyNodes(out, {left25}, mirrored);
    EXPECT_EQ(out.pixelColor(16, 16), QColor(200, 40, 30));  // source x 0.75: outside
    EXPECT_GT(out.pixelColor(48, 16).blue(), 230);           // source x 0.25: inside (shows the blue half)

    // Highlight: the selection in color, the rest mid gray.
    out = img;
    applyNodes(out, {blue}, fullMap(64, 32), nullptr, 0);
    EXPECT_EQ(out.pixelColor(10, 10), QColor(128, 128, 128));
    EXPECT_NE(out.pixelColor(50, 10), QColor(128, 128, 128));
}

TEST(Nodes, PersonAndBackgroundUseTheMask) {
    QImage img(32, 16, QImage::Format_RGB32);
    img.fill(qRgb(100, 100, 100));
    QImage mask(32, 16, QImage::Format_Grayscale8);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x) mask.scanLine(y)[x] = x < 16 ? 255 : 0;  // the person is on the left
    NodeParams person;
    person.subject = 1;
    person.grade.exposure = 0.0;
    timeline::ColorAdjustments::Grade warm;
    warm.temperature = 0.8;
    person.grade = paramsOf(warm);
    QImage out = img;
    applyNodes(out, {person}, fullMap(32, 16), &mask);
    EXPECT_GT(out.pixelColor(4, 8).red(), out.pixelColor(4, 8).blue() + 10);
    EXPECT_EQ(out.pixelColor(28, 8), QColor(100, 100, 100));
    NodeParams background = person;
    background.subject = 2;
    out = img;
    applyNodes(out, {background}, fullMap(32, 16), &mask);
    EXPECT_EQ(out.pixelColor(4, 8), QColor(100, 100, 100));
    EXPECT_GT(out.pixelColor(28, 8).red(), out.pixelColor(28, 8).blue() + 10);
}

TEST(Nodes, ProjectJsonAndRenderPlan) {
    timeline::ColorAdjustments c;
    timeline::ColorAdjustments::Node a;
    a.id = "n1";
    a.label = "Sky";
    a.window.shape = "gradient";
    a.window.rotation = 10;
    a.grade.saturation = 0.3;
    a.grade.lift = {0.2, -0.1, 0.0};
    a.grade.curves[0] = {{0, 0.05}, {1, 0.95}};
    timeline::ColorAdjustments::Node b;
    b.id = "n2";
    b.label = "Pen";
    b.qualifier = qualifierAround(0.15, 0.3, 0.75);
    b.subject = "background";
    b.invert = true;
    timeline::ColorAdjustments::Node off;
    off.id = "n3";
    off.enabled = false;
    off.grade.exposure = 1;
    c.nodes = {a, b, off};
    timeline::Clip clip;
    clip.color = c;
    const auto parsed = timeline::clipFromJson(timeline::toJson(clip), "clip");
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message();
    EXPECT_EQ(parsed->color.nodes, c.nodes);

    const auto nodes = gradeNodes(c);
    ASSERT_EQ(nodes.size(), 2u);  // the disabled node is skipped
    EXPECT_EQ(nodes[0].window.shape, "gradient");
    EXPECT_DOUBLE_EQ(nodes[0].grade.saturation, 0.3);
    EXPECT_EQ(nodes[1].subject, 2);
    EXPECT_TRUE(nodes[1].invert);

    // Bad input: unknown shapes and subjects fall back, at most kMaxNodes are read.
    json::Json j = timeline::toJson(clip);
    j["color"]["nodes"][0]["window"]["shape"] = "star";
    j["color"]["nodes"][1]["subject"] = "cat";
    for (int i = 0; i < 12; ++i) j["color"]["nodes"].push_back(json::Json{{"label", "x"}});
    const auto lenient = timeline::clipFromJson(j, "clip");
    ASSERT_TRUE(lenient.has_value());
    EXPECT_EQ(lenient->color.nodes.size(), timeline::ColorAdjustments::kMaxNodes);
    EXPECT_TRUE(lenient->color.nodes[0].window.isNone());
    EXPECT_TRUE(lenient->color.nodes[1].subject.empty());
    EXPECT_FALSE(lenient->color.nodes[5].id.empty());  // ids are filled in
}
