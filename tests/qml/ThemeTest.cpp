// Light / dark theme: the sun / moon switch changes every themed surface.

#include "qml/QmlHarness.h"

#include <gtest/gtest.h>

using namespace lectern;
using test::QmlHarness;

namespace {
const QByteArray kToggle = R"(
import QtQuick
import Lectern.UI
Item {
    readonly property string mode: Theme.mode
    readonly property color surface: Theme.surface
    readonly property color text: Theme.text
    readonly property color accent: Theme.accent
    ThemeToggle { objectName: "toggle"; x: 20; y: 20 }
}
)";
}  // namespace

TEST(Theme, SunAndMoonSwitchLightAndDark) {
    QmlHarness ui(kToggle, {}, QSize(300, 120));
    ASSERT_TRUE(ui.ok());
    QQuickItem* root = ui.root();
    auto center = [&](const char* name) {
        QQuickItem* item = ui.find(name);
        EXPECT_TRUE(item) << name;
        return item ? QmlHarness::at(item, {item->width() / 2, item->height() / 2}) : QPoint();
    };
    ui.click(center("theme-dark"));
    ASSERT_EQ(root->property("mode").toString(), "dark");
    const QColor darkSurface = root->property("surface").value<QColor>();
    EXPECT_LT(darkSurface.lightness(), 40);
    ui.click(center("theme-light"));
    EXPECT_EQ(root->property("mode").toString(), "light");
    EXPECT_GT(root->property("surface").value<QColor>().lightness(), 220);
    EXPECT_LT(root->property("text").value<QColor>().lightness(), 60);  // dark text on light panels
    EXPECT_EQ(root->property("accent").value<QColor>(), QColor("#3E5BF6"));  // one accent in both modes
    ui.click(center("theme-dark"));
    EXPECT_EQ(root->property("mode").toString(), "dark");
}
