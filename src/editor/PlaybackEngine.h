#pragma once

// Real-time preview with sound (docs/RENDERING_PIPELINE.md §5).
//
// The audio device is the master clock. An audio thread mixes ~150 ms ahead
// into a lock-free ring that the device drains; the clock is "samples the
// device consumed − output latency", so the picture matches what the
// speaker plays. When the mixer falls behind, the device plays silence and
// the clock pauses with it — video waits instead of drifting from audio.
// A video thread renders the frame due at that clock (or on request while
// paused: seeking, scrubbing, edits) with the shared compositor.

#include "audio/AudioOutput.h"
#include "core/SpscRing.h"
#include "project/Project.h"

#include <QImage>
#include <QSize>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace lectern::editor {

class PlaybackEngine {
public:
    /// Called from engine threads; implementations hand off to their own thread.
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void onFrame(const QImage& frame, Time time) = 0;
        virtual void onPlayingChanged(bool playing) = 0;
    };

    PlaybackEngine(std::filesystem::path projectDir, std::unique_ptr<audio::IAudioOutput> output, Listener& listener);
    ~PlaybackEngine();
    PlaybackEngine(const PlaybackEngine&) = delete;
    PlaybackEngine& operator=(const PlaybackEngine&) = delete;

    /// New document snapshot (after every edit). Playback continues; a paused
    /// preview re-renders.
    void setProject(std::shared_ptr<const project::Project> project);
    /// Pixel size of the preview frames.
    void setPreviewSize(QSize size);
    void play();
    void pause();
    void seek(Time t);
    /// Re-renders the current frame (e.g. after the canvas became visible).
    void refresh();

    /// Before / after view of the grade (Color page). Bypass shows every clip
    /// ungraded (input color management stays); Wipe shows the ungraded
    /// picture left of `split` (0…1 of the width) and the graded one right of
    /// it; SideBySide shows ungraded left and graded right at half size.
    enum class Compare { Off, Bypass, Wipe, SideBySide };
    void setCompare(Compare mode, double split = 0.5);
    /// Puts `before` and `after` (rendered at the canvas size for Wipe, half
    /// size for SideBySide) into `out` for `mode`.
    static void composeComparison(QImage& out, const QImage& before, const QImage& after, Compare mode, double split);

    [[nodiscard]] bool playing() const;
    [[nodiscard]] Time position() const;
    [[nodiscard]] Time duration() const;
    /// Peak level of what the device played recently, per channel (0…1).
    [[nodiscard]] float level(int channel) const;
    [[nodiscard]] std::string outputName() const;
    [[nodiscard]] std::uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t framesRendered() const { return framesRendered_.load(std::memory_order_relaxed); }

private:
    void render(float* out, int frames) noexcept;  // real-time audio thread
    void audioLoop(std::stop_token stop);
    void videoLoop(std::stop_token stop);
    [[nodiscard]] Time positionLocked() const;
    void restartLocked(Time from);
    void stopDeviceLocked();
    [[nodiscard]] Time durationLocked() const;

    std::filesystem::path dir_;
    Listener& listener_;

    mutable std::mutex mutex_;
    std::condition_variable_any videoCv_;
    std::condition_variable_any audioCv_;
    std::unique_ptr<audio::IAudioOutput> output_;  // guarded (start/stop)
    std::shared_ptr<const project::Project> project_;
    QSize previewSize_{960, 540};
    Compare compare_ = Compare::Off;
    double compareSplit_ = 0.5;
    bool playing_ = false;
    bool deviceRunning_ = false;
    bool renderRequested_ = true;
    Time anchor_;                  ///< timeline time of the paused position / start of the current run
    std::int64_t mixPosition_ = 0;  ///< next timeline sample to mix
    std::uint64_t generation_ = 0;  ///< bumps on every restart; stale mixes are dropped
    Time latency_;

    SpscRing<float> ring_;
    std::atomic<bool> playingFlag_{false};
    std::atomic<bool> endOfStream_{false};  ///< everything up to the end is mixed (an empty ring is no underrun)
    std::atomic<std::int64_t> framesPlayed_{0};  ///< frames the device consumed since the anchor
    std::atomic<std::int64_t> lastCallbackNs_{0};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> framesRendered_{0};
    std::atomic<float> levels_[2] = {0.0f, 0.0f};

    std::jthread audioThread_;
    std::jthread videoThread_;
};

}  // namespace lectern::editor
