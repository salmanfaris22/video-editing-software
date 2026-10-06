#include "media/VideoFramePool.h"

#include <gtest/gtest.h>

#include <cstring>
#include <thread>
#include <vector>

using namespace lectern;
using namespace lectern::media;

TEST(VideoFramePool, LaysOutAlignedPlanesInOneBuffer) {
    auto pool = VideoFramePool::create(1366, 768, AV_PIX_FMT_NV12);  // width not a multiple of 64
    ASSERT_TRUE(pool) << pool.error().toString();
    auto f = (*pool)->acquire();
    ASSERT_TRUE(f) << f.error().toString();
    AVFrame* av = f->get();
    EXPECT_EQ(av->width, 1366);
    EXPECT_EQ(av->height, 768);
    EXPECT_EQ(av->format, AV_PIX_FMT_NV12);
    for (int plane : {0, 1}) {
        EXPECT_EQ(av->linesize[plane] % 64, 0);
        EXPECT_GE(av->linesize[plane], 1366);
    }
    EXPECT_EQ(av->buf[1], nullptr);  // one allocation for all planes
    EXPECT_GE(av->data[1] - av->data[0], static_cast<std::ptrdiff_t>(av->linesize[0]) * 768);
    const std::uint8_t* end = av->buf[0]->data + av->buf[0]->size;
    EXPECT_LE(av->data[1] + static_cast<std::ptrdiff_t>(av->linesize[1]) * (768 / 2), end);
    EXPECT_TRUE(av_frame_is_writable(av));
}

TEST(VideoFramePool, RecyclesReleasedBuffers) {
    auto pool = VideoFramePool::create(640, 360, AV_PIX_FMT_NV12);
    ASSERT_TRUE(pool);
    const std::uint8_t* first = nullptr;
    {
        auto f = (*pool)->acquire();
        ASSERT_TRUE(f);
        first = (*f)->data[0];
    }
    auto again = (*pool)->acquire();
    ASSERT_TRUE(again);
    EXPECT_EQ((*again)->data[0], first);  // the released buffer came back
    auto second = (*pool)->acquire();
    ASSERT_TRUE(second);
    EXPECT_NE((*second)->data[0], (*again)->data[0]);  // a held buffer is never handed out twice
}

TEST(VideoFramePool, FramesOutliveThePool) {
    Frame survivor;
    {
        auto pool = VideoFramePool::create(320, 240, AV_PIX_FMT_YUV420P);
        ASSERT_TRUE(pool);
        auto f = (*pool)->acquire();
        ASSERT_TRUE(f);
        survivor = std::move(*f);
    }
    // Under ASan this would flag a use-after-free if the pool freed early.
    std::memset(survivor->data[0], 0x80, static_cast<std::size_t>(survivor->linesize[0]) * 240);
    std::memset(survivor->data[2], 0x80, static_cast<std::size_t>(survivor->linesize[2]) * 120);
    survivor.reset();
}

TEST(VideoFramePool, PooledFramesConvertWithSwscale) {
    auto pool = VideoFramePool::create(64, 32, AV_PIX_FMT_NV12);
    ASSERT_TRUE(pool);
    auto src = (*pool)->acquire();
    ASSERT_TRUE(src);
    for (int y = 0; y < 32; ++y) std::memset((*src)->data[0] + y * (*src)->linesize[0], 100, 64);
    for (int y = 0; y < 16; ++y) std::memset((*src)->data[1] + y * (*src)->linesize[1], 128, 64);
    auto dst = Frame::allocVideo(64, 32, AV_PIX_FMT_YUV420P);
    ASSERT_TRUE(dst);
    SwsContextPtr sws(sws_getContext(64, 32, AV_PIX_FMT_NV12, 64, 32, AV_PIX_FMT_YUV420P, SWS_POINT, nullptr, nullptr,
                                     nullptr));
    ASSERT_TRUE(sws);
    sws_scale(sws.get(), (*src)->data, (*src)->linesize, 0, 32, (*dst)->data, (*dst)->linesize);
    EXPECT_EQ((*dst)->data[0][63], 100);
    EXPECT_EQ((*dst)->data[1][0], 128);
    EXPECT_EQ((*dst)->data[2][31], 128);
}

TEST(VideoFramePool, RejectsInvalidAndHardwareFormats) {
    EXPECT_FALSE(VideoFramePool::create(0, 10, AV_PIX_FMT_NV12));
    EXPECT_FALSE(VideoFramePool::create(64, 64, AV_PIX_FMT_NONE));
    EXPECT_FALSE(VideoFramePool::create(64, 64, AV_PIX_FMT_D3D11));
    EXPECT_FALSE(VideoFramePool::create(64, 64, AV_PIX_FMT_PAL8));
    EXPECT_TRUE(VideoFramePool::create(64, 64, AV_PIX_FMT_BGRA));
}

TEST(VideoFramePool, ConcurrentAcquireAndRelease) {
    auto pool = VideoFramePool::create(128, 72, AV_PIX_FMT_NV12);
    ASSERT_TRUE(pool);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&pool, t] {
            for (int i = 0; i < 500; ++i) {
                auto f = (*pool)->acquire();
                ASSERT_TRUE(f);
                (*f)->data[0][0] = static_cast<std::uint8_t>(t);
                Frame shared = f->ref();  // a second owner; the buffer returns when both are gone
                ASSERT_TRUE(shared);
            }
        });
    }
    for (auto& th : threads) th.join();
}
