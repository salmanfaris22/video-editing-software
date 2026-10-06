#include "media/MediaProbe.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <fstream>

using namespace lectern;

TEST(MediaProbe, MissingAndCorruptFilesFailGracefully) {
    test::TempDir dir;
    auto missing = media::probeMedia(dir / "nope.mp4");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), ErrorCode::NotFound);

    std::ofstream(dir / "bad.mp4") << std::string(4096, '\x42');
    auto bad = media::probeMedia(dir / "bad.mp4");
    ASSERT_FALSE(bad);
}

TEST(MediaProbe, DetectsStillImages) {
    test::TempDir dir;
    // Minimal 1x1 PNG.
    static const unsigned char kPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF,
        0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    std::ofstream(dir / "pixel.png", std::ios::binary).write(reinterpret_cast<const char*>(kPng), sizeof kPng);
    auto info = media::probeMedia(dir / "pixel.png");
    ASSERT_TRUE(info) << info.error().toString();
    EXPECT_EQ(info->kind, media::MediaKind::Image);
    ASSERT_NE(info->video(), nullptr);
    EXPECT_EQ(info->video()->video->width, 1);
    const auto j = media::toJson(*info);
    EXPECT_EQ(j["kind"], "image");
}
