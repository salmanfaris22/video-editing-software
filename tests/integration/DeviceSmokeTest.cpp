// Real-device smoke tests (label "device"; excluded from the default test
// presets). They only record when the OS has already granted permission and
// never trigger a permission prompt. Microphone capture additionally requires
// LECTERN_TEST_MICROPHONE=1.

#include "capture/RecordingSession.h"
#include "media/FFmpeg.h"
#include "media/MediaProbe.h"
#include "platform/AudioOutput.h"
#include "platform/GlobalHotkeys.h"
#include "platform/PlatformBackends.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <thread>

using namespace lectern;
using namespace lectern::capture;

namespace {

struct Backends {
    CaptureBackends b;
    Backends() {
        Logger::instance().addSink(makeStderrSink());
        Logger::instance().setLevel(LogLevel::Info);
        media::initializeFFmpeg(LogLevel::Warn);
        platform::initializePlatform();
        b = platform::createCaptureBackends();
    }
};

Backends& backends() {
    static Backends instance;
    return instance;
}

SessionResult recordSeconds(std::vector<TrackPlan> tracks, double seconds, const test::TempDir& dir) {
    SessionConfig cfg;
    cfg.title = "Device smoke test";
    cfg.projectDir = dir / "project.lectern";
    cfg.registryDir = dir / "registry";
    cfg.tracks = std::move(tracks);
    auto session = RecordingSession::create(std::move(cfg));
    EXPECT_TRUE(session);
    EXPECT_TRUE((*session)->start());
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    const RecordingStats stats = (*session)->stats();
    for (const auto& t : stats.tracks) {
        if (t.video) {
            std::printf("[%s] encoder=%s hw=%d zeroCopy=%d frames=%llu dup=%llu dropped=%llu %dx%d\n", t.id.c_str(),
                        t.video->encoder.c_str(), t.video->hardwareEncoder, t.video->zeroCopy,
                        static_cast<unsigned long long>(t.video->framesEncoded),
                        static_cast<unsigned long long>(t.video->framesDuplicated),
                        static_cast<unsigned long long>(t.video->droppedQueueFull + t.video->droppedBySource),
                        t.video->width, t.video->height);
        }
    }
    EXPECT_TRUE((*session)->stop());
    auto r = (*session)->waitForCompletion(std::chrono::seconds(30));
    EXPECT_TRUE(r) << (r ? "" : r.error().toString());
    return r ? *r : SessionResult{};
}

}  // namespace

TEST(DeviceSmoke, EnumeratesDevices) {
    auto& b = backends().b;
    const auto info = platform::platformInfo();
    std::printf("platform: %s native screen=%d camera=%d audio=%d system-audio=%d\n", info.os.c_str(),
                info.nativeScreen, info.nativeCamera, info.nativeAudio, info.nativeSystemAudio);
    for (auto kind : {PermissionKind::ScreenCapture, PermissionKind::Camera, PermissionKind::Microphone}) {
        std::printf("permission %s: %s\n", std::string(toString(kind)).c_str(),
                    std::string(toString(b.permissions->status(kind))).c_str());
    }
    auto cams = b.camera->enumerateCameras();
    ASSERT_TRUE(cams) << cams.error().toString();
    for (const auto& c : *cams) std::printf("camera: %s [%s] formats=%zu\n", c.name.c_str(), c.id.c_str(), c.formats.size());
    auto mics = b.audio->enumerateInputs();
    ASSERT_TRUE(mics) << mics.error().toString();
    for (const auto& m : *mics) {
        std::printf("microphone: %s [%s] %d ch @ %d Hz%s\n", m.name.c_str(), m.id.c_str(), m.channels, m.sampleRate,
                    m.isDefault ? " (default)" : "");
    }
    if (b.permissions->status(PermissionKind::ScreenCapture) == PermissionStatus::Granted) {
        auto targets = b.screen->enumerateTargets();
        ASSERT_TRUE(targets) << targets.error().toString();
        EXPECT_FALSE(targets->displays.empty());
        for (const auto& d : targets->displays) {
            std::printf("display: %s [%s] %dx%d%s\n", d.name.c_str(), d.id.c_str(), d.widthPx, d.heightPx,
                        d.isMain ? " (main)" : "");
        }
        std::printf("windows: %zu, applications: %zu\n", targets->windows.size(), targets->applications.size());
    }
}

TEST(DeviceSmoke, RecordsMainDisplay) {
    auto& b = backends().b;
    if (!platform::platformInfo().nativeScreen) GTEST_SKIP() << "no native screen backend";
    if (b.permissions->status(PermissionKind::ScreenCapture) != PermissionStatus::Granted) {
        GTEST_SKIP() << "screen recording permission not granted";
    }
    auto targets = b.screen->enumerateTargets();
    ASSERT_TRUE(targets && !targets->displays.empty());
    ScreenCaptureConfig cfg;
    cfg.target = {ScreenTargetKind::Display, targets->displays.front().id, {}};
    cfg.maxWidth = 1920;
    cfg.maxHeight = 1080;
    cfg.frameRate = FrameRate(30, 1);
    auto source = b.screen->createSource(cfg);
    ASSERT_TRUE(source) << source.error().toString();
    auto live = LiveVideoSource::start(std::move(*source));
    ASSERT_TRUE(live) << live.error().toString();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    test::TempDir dir("lectern-device");
    TrackPlan screen;
    screen.trackId = "screen";
    screen.role = TrackRole::Screen;
    screen.videoSource = *live;
    screen.video.frameRate = FrameRate(30, 1);
    screen.video.bitRate = 8'000'000;
    const SessionResult r = recordSeconds({screen}, 3.0, dir);
    (*live)->stop();
    ASSERT_EQ(r.state, SessionState::Completed);
    auto decoded = test::decodeVideo(r.projectDir / r.manifest.tracks.at(0).file);
    ASSERT_TRUE(decoded) << decoded.error().toString();
    std::printf("decoded %zu frames %dx%d\n", decoded->ptsSeconds.size(), decoded->width, decoded->height);
    EXPECT_NEAR(static_cast<double>(decoded->ptsSeconds.size()), 90.0, 3.0);
    EXPECT_GT(decoded->width, 0);
}

TEST(DeviceSmoke, RecordsDefaultCamera) {
    auto& b = backends().b;
    if (!platform::platformInfo().nativeCamera) GTEST_SKIP() << "no native camera backend";
    if (b.permissions->status(PermissionKind::Camera) != PermissionStatus::Granted) {
        GTEST_SKIP() << "camera permission not granted";
    }
    auto cams = b.camera->enumerateCameras();
    if (!cams || cams->empty()) GTEST_SKIP() << "no camera";
    auto source = b.camera->createSource(CameraCaptureConfig{cams->front().id, 1280, 720, FrameRate(30, 1)});
    ASSERT_TRUE(source) << source.error().toString();
    auto live = LiveVideoSource::start(std::move(*source));
    ASSERT_TRUE(live) << live.error().toString();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));  // camera warm-up

    test::TempDir dir("lectern-device");
    TrackPlan cam;
    cam.trackId = "camera";
    cam.role = TrackRole::Camera;
    cam.videoSource = *live;
    cam.video.frameRate = FrameRate(30, 1);
    cam.video.maxHold = Time::fromSeconds(2);
    const SessionResult r = recordSeconds({cam}, 2.0, dir);
    (*live)->stop();
    ASSERT_EQ(r.state, SessionState::Completed);
    auto decoded = test::decodeVideo(r.projectDir / r.manifest.tracks.at(0).file);
    ASSERT_TRUE(decoded) << decoded.error().toString();
    std::printf("camera: decoded %zu frames %dx%d\n", decoded->ptsSeconds.size(), decoded->width, decoded->height);
    EXPECT_GT(decoded->ptsSeconds.size(), 40u);
}

TEST(DeviceSmoke, RecordsDefaultMicrophone) {
    auto& b = backends().b;
    if (!std::getenv("LECTERN_TEST_MICROPHONE")) GTEST_SKIP() << "set LECTERN_TEST_MICROPHONE=1 to record the mic";
    if (b.permissions->status(PermissionKind::Microphone) != PermissionStatus::Granted) {
        GTEST_SKIP() << "microphone permission not granted";
    }
    auto source = b.audio->createMicrophoneSource({});
    ASSERT_TRUE(source) << source.error().toString();
    auto live = LiveAudioSource::start(std::move(*source));
    ASSERT_TRUE(live) << live.error().toString();
    test::TempDir dir("lectern-device");
    TrackPlan mic;
    mic.trackId = "microphone";
    mic.role = TrackRole::Microphone;
    mic.audioSource = *live;
    const SessionResult r = recordSeconds({mic}, 2.0, dir);
    (*live)->stop();
    ASSERT_EQ(r.state, SessionState::Completed);
    auto decoded = test::decodeAudio(r.projectDir / r.manifest.tracks.at(0).file);
    ASSERT_TRUE(decoded);
    EXPECT_NEAR(decoded->startSeconds + static_cast<double>(decoded->channel0.size()) / decoded->sampleRate, 2.0, 0.05);
}

TEST(DeviceSmoke, RegistersAndRemovesGlobalHotkeys) {
    auto hotkeys = platform::createGlobalHotkeys();
    if (!hotkeys) GTEST_SKIP() << "no global hotkey support on this platform yet";
    // An unusual combination so the test never collides with real shortcuts.
    const platform::HotkeySpec spec{'9', platform::kHotkeyCommand | platform::kHotkeyShift | platform::kHotkeyOption |
                                             platform::kHotkeyControl};
    auto st = hotkeys->add(42, spec, [] {});
    ASSERT_TRUE(st) << st.error().toString();
    auto again = hotkeys->add(42, spec, [] {});  // re-registering the same id replaces it
    EXPECT_TRUE(again) << (again ? "" : again.error().toString());
    EXPECT_FALSE(hotkeys->add(43, {'#', platform::kHotkeyCommand}, [] {}));  // unsupported key
    hotkeys->removeAll();
}

TEST(DeviceSmoke, AudioOutputPullsAudioInRealTime) {
    // Plays digital silence only: inaudible, but exercises the real device path.
    auto output = platform::createAudioOutput();
    ASSERT_TRUE(output);
    std::atomic<std::int64_t> frames{0};
    auto st = output->start([&frames](float* out, int n) {
        std::fill(out, out + static_cast<std::ptrdiff_t>(n) * 2, 0.0f);
        frames.fetch_add(n, std::memory_order_relaxed);
    });
    if (!st) GTEST_SKIP() << "no audio output: " << st.error().toString();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    output->stop();
    const std::int64_t pulled = frames.load();
    std::printf("audio output '%s': latency %.1f ms, pulled %lld frames in 0.5 s\n", output->deviceName().c_str(),
                output->latency().toSecondsF() * 1000, static_cast<long long>(pulled));
    EXPECT_GT(pulled, 48'000 * 0.5 * 0.8);
    EXPECT_LT(pulled, 48'000 * 0.5 * 1.3);
    const std::int64_t after = frames.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(frames.load(), after);  // stop() is final
}
