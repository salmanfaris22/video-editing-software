#include "editor/EditorFixture.h"
#include "editor/Exporter.h"
#include "media/MediaProbe.h"

#include <gtest/gtest.h>

using namespace lectern;
using namespace lectern::editor;
using namespace lectern::timeline;

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

}  // namespace

TEST(QaRegression, MkvAndMovExportsProbeClean) {
    test::EditorFixture f({.seconds = 0.4});
    for (const char* ext : {"mkv", "mov"}) {
        const auto out = f.dir / (std::string("out.") + ext);
        auto result = exportProject(f.project, f.dir.path(),
                                    {.output = out, .width = 640, .height = 360, .container = ext, .hardwareEncoder = false});
        ASSERT_TRUE(result) << ext << ": " << result.error().toString();
        auto info = media::probeMedia(out);
        ASSERT_TRUE(info) << ext;
        EXPECT_EQ(info->video()->codec, "h264");
    }
}

TEST(QaRegression, ScreenClipOpacityKeyframesScrubAtPlayhead) {
    test::EditorFixture f({.seconds = 2.0});
    ClipId screen;
    for (const Track& t : f.project.timeline.tracks) {
        for (const Clip& c : t.clips) {
            if (c.kind == ClipKind::Media) screen = c.id;
        }
    }
    const Clip* clip = f.project.timeline.findClip(screen);
    ASSERT_TRUE(clip);
    Clip edited = *clip;
    edited.opacity.setKey(sec(0), 1.0);
    edited.opacity.setKey(sec(1), 0.25);
    EXPECT_NEAR(edited.opacity.evaluate(sec(0.5)), 0.625, 1e-3);
    EXPECT_NEAR(edited.sourceTimeAt(sec(0.5)).toSecondsF(), 0.5, 1e-3);
}
