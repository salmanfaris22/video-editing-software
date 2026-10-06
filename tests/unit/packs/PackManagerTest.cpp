#include "packs/PackManager.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

using namespace lectern::packs;
using lectern::test::TempDir;

TEST(PackManager, InstallsEmbeddedWhisperStub) {
    TempDir tmp;
    PackManager pm(tmp.path() / "packs");
    auto path = pm.require(kWhisperBasePackId);
    ASSERT_TRUE(path) << path.error().toString();
    EXPECT_TRUE(std::filesystem::exists(*path / "model.stub"));
    EXPECT_EQ(pm.state(kWhisperBasePackId, "1.0.0"), PackState::Installed);
}
