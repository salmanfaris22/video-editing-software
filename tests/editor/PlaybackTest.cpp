#include "editor/EditorFixture.h"
#include "editor/PlaybackEngine.h"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace lectern;
using namespace lectern::editor;
using namespace std::chrono_literals;

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

/// A real-time output device that keeps everything it played.
class RecordingOutput final : public audio::IAudioOutput {
public:
    explicit RecordingOutput(bool failToStart = false) : failToStart_(failToStart) {}
    ~RecordingOutput() override { stop(); }
    Status start(RenderCallback render) override {
        if (failToStart_) return fail(ErrorCode::NotFound, "no device");
        stop();
        render_ = std::move(render);
        running_ = true;
        thread_ = std::thread([this] {
            std::vector<float> block(480 * 2);
            auto next = std::chrono::steady_clock::now();
            while (running_) {
                render_(block.data(), 480);
                {
                    std::lock_guard lock(mutex_);
                    played_.insert(played_.end(), block.begin(), block.end());
                }
                next += 10ms;
                std::this_thread::sleep_until(next);
            }
        });
        ++starts_;
        return ok();
    }
    void stop() override {
        if (!running_.exchange(false)) return;
        thread_.join();
    }
    [[nodiscard]] Time latency() const override { return Time::zero(); }
    [[nodiscard]] std::string deviceName() const override { return "test"; }
    std::vector<float> played() {
        std::lock_guard lock(mutex_);
        return played_;
    }
    void clear() {
        std::lock_guard lock(mutex_);
        played_.clear();
    }
    std::atomic<int> starts_{0};

private:
    bool failToStart_;
    RenderCallback render_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::vector<float> played_;
};

class Listener final : public PlaybackEngine::Listener {
public:
    void onFrame(const QImage& frame, Time t) override {
        std::lock_guard lock(mutex_);
        times.push_back(t);
        lastSize = frame.size();
        cv_.notify_all();
    }
    void onPlayingChanged(bool playing) override {
        std::lock_guard lock(mutex_);
        states.push_back(playing);
        cv_.notify_all();
    }
    bool waitFor(const std::function<bool()>& pred, std::chrono::milliseconds timeout = 3000ms) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, pred);
    }
    std::size_t frameCount() {
        std::lock_guard lock(mutex_);
        return times.size();
    }
    std::vector<Time> times;
    std::vector<bool> states;
    QSize lastSize;
    std::mutex mutex_;

private:
    std::condition_variable cv_;
};

/// Index where `played` (left channel) starts following the ramp from `sample`; -1 if never.
std::int64_t findRamp(const std::vector<float>& played, std::int64_t sample, int length = 2000) {
    const auto frames = static_cast<std::int64_t>(played.size() / 2);
    for (std::int64_t i = 0; i + length <= frames; ++i) {
        if (std::fabs(played[static_cast<std::size_t>(2 * i)] - test::rampSample(sample)) > 2e-5f) continue;
        bool match = true;
        for (int k = 1; k < length && match; ++k) {
            match = std::fabs(played[static_cast<std::size_t>(2 * (i + k))] - test::rampSample(sample + k)) <= 2e-5f;
        }
        if (match) return i;
    }
    return -1;
}

}  // namespace

TEST(Playback, PlaysInRealTimeWithTheSoundInSync) {
    test::EditorFixture f;
    auto output = std::make_unique<RecordingOutput>();
    RecordingOutput* device = output.get();
    Listener listener;
    PlaybackEngine engine(f.dir.path(), std::move(output), listener);
    engine.setPreviewSize({320, 180});
    engine.setProject(f.snapshot());
    ASSERT_TRUE(listener.waitFor([&] { return !listener.times.empty(); }));  // the paused preview renders
    EXPECT_EQ(listener.lastSize, QSize(320, 180));

    const std::size_t before = listener.frameCount();
    engine.play();
    std::this_thread::sleep_for(1000ms);
    const Time at = engine.position();
    const std::size_t playedFrames = device->played().size() / 2;
    engine.pause();
    // The playhead is where the speaker is: what the device consumed so far.
    EXPECT_NEAR(at.toSecondsF(), static_cast<double>(playedFrames) / 48'000, 0.06);
    EXPECT_GT(at.toSecondsF(), 0.6);  // and it really advanced in (about) real time
    EXPECT_LT(at.toSecondsF(), 1.1);
    const std::size_t played = listener.frameCount() - before;
    EXPECT_GE(played, 20u);  // ~30 fps preview
    EXPECT_LE(played, 40u);

    // The device played the mix exactly: the microphone ramp from sample 0, gapless.
    const std::vector<float> audio = device->played();
    ASSERT_GE(audio.size(), 48'000u * 2 / 2);
    EXPECT_EQ(findRamp(audio, 0, 24'000), 0);

    const Time paused = engine.position();
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(engine.position(), paused);
    EXPECT_FALSE(engine.playing());
    std::lock_guard lock(listener.mutex_);
    ASSERT_EQ(listener.states.size(), 2u);
    EXPECT_TRUE(listener.states[0]);
    EXPECT_FALSE(listener.states[1]);
}

TEST(Playback, SeekWhilePlayingContinuesFromTheNewTimeWithItsSound) {
    test::EditorFixture f;
    auto output = std::make_unique<RecordingOutput>();
    RecordingOutput* device = output.get();
    Listener listener;
    PlaybackEngine engine(f.dir.path(), std::move(output), listener);
    engine.setProject(f.snapshot());
    engine.play();
    std::this_thread::sleep_for(300ms);
    engine.seek(sec(2.0));
    std::this_thread::sleep_for(400ms);
    const Time position = engine.position();
    const std::vector<float> audio = device->played();
    engine.pause();

    // The device played: the ramp from 0 (before the seek), possibly a little
    // silence while it restarted, then the ramp from 2.0 s — gapless, and
    // never a stale sample from before the seek.
    const auto frames = static_cast<std::int64_t>(audio.size() / 2);
    std::int64_t before = 0;
    while (before < frames && std::fabs(audio[static_cast<std::size_t>(2 * before)] - test::rampSample(before)) <= 2e-5f) ++before;
    ASSERT_GT(before, 4800);  // ≥ 0.1 s played before the seek
    std::int64_t after = before;
    while (after < frames && audio[static_cast<std::size_t>(2 * after)] == 0.0f) ++after;
    ASSERT_LT(after, frames);
    for (std::int64_t i = after; i < frames; ++i) {
        ASSERT_NEAR(audio[static_cast<std::size_t>(2 * i)], test::rampSample(96'000 + i - after), 2e-5f) << i;
    }
    // A/V sync: the playhead is exactly where the speaker is (whatever the machine load).
    const double played = 2.0 + static_cast<double>(frames - after) / 48'000;
    EXPECT_NEAR(position.toSecondsF(), played, 0.06);
    EXPECT_GT(position.toSecondsF(), 2.1);
}

TEST(Playback, StopsAtTheEndAndReplaysFromTheStart) {
    test::EditorFixture f({.seconds = 1.0});
    Listener listener;
    PlaybackEngine engine(f.dir.path(), std::make_unique<RecordingOutput>(), listener);
    engine.setProject(f.snapshot());
    engine.seek(sec(0.7));
    engine.play();
    ASSERT_TRUE(listener.waitFor([&] { return listener.states.size() >= 2; }, 2000ms));
    EXPECT_FALSE(engine.playing());
    EXPECT_EQ(engine.position(), engine.duration());
    EXPECT_EQ(engine.underruns(), 0u);
    engine.play();  // at the end: starts over
    std::this_thread::sleep_for(150ms);
    EXPECT_LT(engine.position().toSecondsF(), 0.5);
    engine.pause();
}

TEST(Playback, PausedSeeksAndEditsRenderTheRequestedFrame) {
    test::EditorFixture f;
    Listener listener;
    PlaybackEngine engine(f.dir.path(), std::make_unique<RecordingOutput>(), listener);
    engine.setProject(f.snapshot());
    ASSERT_TRUE(listener.waitFor([&] { return !listener.times.empty(); }));
    engine.seek(sec(1.5));
    ASSERT_TRUE(listener.waitFor([&] { return listener.times.back() == sec(1.5); }));
    const std::size_t count = listener.frameCount();
    auto edited = std::make_shared<project::Project>(f.project);
    edited->style.screenPadding = 0.05;
    engine.setProject(edited);
    ASSERT_TRUE(listener.waitFor([&] { return listener.times.size() > count; }));
    EXPECT_EQ(listener.times.back(), sec(1.5));
}

TEST(Playback, FallsBackToASilentClockWithoutAnAudioDevice) {
    test::EditorFixture f;
    Listener listener;
    PlaybackEngine engine(f.dir.path(), std::make_unique<RecordingOutput>(/*failToStart=*/true), listener);
    engine.setProject(f.snapshot());
    engine.play();
    std::this_thread::sleep_for(500ms);
    EXPECT_NEAR(engine.position().toSecondsF(), 0.5, 0.15);
    EXPECT_EQ(engine.outputName(), "No audio device");
    engine.pause();
}
