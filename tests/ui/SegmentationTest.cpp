// Apple Vision person segmentation behind the camera background blur.
// LECTERN_TEST_PERSON_IMAGE=<photo of a person, centered> also checks that
// the person is found.

#include "editor/Compositor.h"
#include "editor/PlatformSegmenter.h"
#include "platform/PersonSegmentation.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>

#include <gtest/gtest.h>

using namespace lectern;

namespace {

double coverage(const QImage& mask, QRectF area) {  // mean mask value in a fractional area, 0…1
    const QRect r(int(area.x() * mask.width()), int(area.y() * mask.height()), int(area.width() * mask.width()),
                  int(area.height() * mask.height()));
    double sum = 0;
    int n = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            sum += qGray(mask.pixel(x, y)) / 255.0;
            ++n;
        }
    }
    return n ? sum / n : 0;
}

}  // namespace

TEST(PersonSegmentation, AnEmptyRoomHasNoPerson) {
    if (!platform::personSegmentationAvailable()) GTEST_SKIP() << "not available on this system";
    editor::installPlatformSegmenter();
    ASSERT_TRUE(editor::hasPersonSegmenter());
    QImage room(640, 360, QImage::Format_RGB32);
    QPainter p(&room);
    QLinearGradient g(0, 0, 640, 360);
    g.setColorAt(0, QColor(90, 110, 140));
    g.setColorAt(1, QColor(200, 190, 170));
    p.fillRect(room.rect(), g);
    p.end();
    auto mask = platform::segmentPeople(room.constBits(), room.width(), room.height(), static_cast<int>(room.bytesPerLine()));
    ASSERT_TRUE(mask) << mask.error().toString();
    EXPECT_GT(mask->width, 0);
    EXPECT_GT(mask->height, 0);
    double mean = 0;
    for (std::uint8_t v : mask->alpha) mean += v;
    mean /= 255.0 * static_cast<double>(mask->alpha.size());
    EXPECT_LT(mean, 0.05);
    editor::setPersonSegmenter({});
}

TEST(PersonSegmentation, FindsThePersonInAPhoto) {
    const QByteArray path = qgetenv("LECTERN_TEST_PERSON_IMAGE");
    if (path.isEmpty()) GTEST_SKIP() << "set LECTERN_TEST_PERSON_IMAGE to a portrait photo";
    if (!platform::personSegmentationAvailable()) GTEST_SKIP() << "not available on this system";
    const QImage photo = QImage(QString::fromLocal8Bit(path)).convertToFormat(QImage::Format_RGB32);
    ASSERT_FALSE(photo.isNull());
    editor::installPlatformSegmenter();
    // Through the compositor's segmenter (as the background blur uses it).
    editor::RenderPlan plan;
    plan.width = photo.width();
    plan.height = photo.height();
    auto mask = platform::segmentPeople(photo.constBits(), photo.width(), photo.height(), static_cast<int>(photo.bytesPerLine()));
    ASSERT_TRUE(mask) << mask.error().toString();
    QImage m(mask->width, mask->height, QImage::Format_Grayscale8);
    for (int y = 0; y < mask->height; ++y) {
        std::memcpy(m.scanLine(y), mask->alpha.data() + static_cast<std::size_t>(y) * mask->width, static_cast<std::size_t>(mask->width));
    }
    const double person = coverage(m, QRectF(0.35, 0.3, 0.3, 0.4));
    const double corner = coverage(m, QRectF(0.0, 0.0, 0.12, 0.12));
    std::printf("person coverage %.2f, corner %.2f, mask %dx%d\n", person, corner, mask->width, mask->height);
    EXPECT_GT(person, 0.6);
    EXPECT_LT(corner, 0.2);

    // The camera background blur on that photo: the person stays sharp.
    editor::VisualLayer layer;
    layer.role = "camera";
    layer.fill = true;
    layer.box = {0, 0, 1, 1};
    layer.backgroundBlur = 1.0;
    plan.layers.push_back(layer);
    QImage out(photo.size(), QImage::Format_RGB32);
    editor::Compositor compositor;
    compositor.render(plan, out, [&](const editor::VisualLayer&, QSizeF) { return photo; });
    auto sharpness = [](const QImage& img, QRect r) {
        double sum = 0;
        for (int y = r.top(); y <= r.bottom(); ++y) {
            for (int x = r.left(); x < r.right(); ++x) sum += std::abs(qGray(img.pixel(x, y)) - qGray(img.pixel(x + 1, y)));
        }
        return sum / (r.width() * r.height());
    };
    const QRect face(int(photo.width() * 0.4), int(photo.height() * 0.3), int(photo.width() * 0.2), int(photo.height() * 0.2));
    const QRect wall(0, 0, int(photo.width() * 0.12), int(photo.height() * 0.12));
    EXPECT_NEAR(sharpness(out, face), sharpness(photo, face), sharpness(photo, face) * 0.15);  // unchanged
    EXPECT_LT(sharpness(out, wall), sharpness(photo, wall) * 0.5);                               // softened
    if (const QByteArray dir = qgetenv("LECTERN_TEST_DUMP_DIR"); !dir.isEmpty()) {
        out.save(QString::fromLocal8Bit(dir) + QStringLiteral("/background-blur.png"));
    }
    editor::setPersonSegmenter({});
}
