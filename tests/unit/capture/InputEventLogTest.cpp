#include "capture/SessionManifest.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

using namespace lectern::capture;
using lectern::Time;
using lectern::test::TempDir;

TEST(InputEventLog, RoundTripThroughJson) {
    InputEventLog log;
    log.captureWidth = 1920;
    log.captureHeight = 1080;
    log.pointer.push_back({Time::fromSecondsF(1.0), "down", 0.5, 0.4, 0});
    log.keys.push_back({Time::fromSecondsF(1.1), "down", "a", 0});

    TempDir dir;
    const auto path = dir.path() / "input-events.json";
    ASSERT_TRUE(writeInputEventLog(path, log));
    auto read = readInputEventLog(path);
    ASSERT_TRUE(read) << read.error().toString();
    ASSERT_EQ(read->pointer.size(), 1u);
    EXPECT_EQ(read->pointer[0].type, "down");
    EXPECT_NEAR(read->pointer[0].x, 0.5, 1e-9);
    ASSERT_EQ(read->keys.size(), 1u);
    EXPECT_EQ(read->keys[0].key, "a");
}
