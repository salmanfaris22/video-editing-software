#include "editor/PlaybackEngine.h"

#include "core/Clock.h"
#include "core/Log.h"
#include "core/Thread.h"
#include "editor/AudioMixer.h"
#include "editor/FrameRenderer.h"
#include "editor/FrameProvider.h"
#include "editor/RenderPlan.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lectern::editor {

namespace {
constexpr int kRate = audio::IAudioOutput::kSampleRate;
constexpr int kChannels = audio::IAudioOutput::kChannels;
constexpr std::size_t kAheadFrames = kRate * 15 / 100;   // keep ~150 ms mixed ahead
constexpr std::size_t kPrefillFrames = kRate * 6 / 100;  // start the device once 60 ms are ready
constexpr int kMixBlock = 1024;
constexpr Time kMaxInterpolation = Time::fromMilliseconds(25);

FrameRate previewRate(const project::Project& p) {
    const double fps = p.canvas.frameRate.toDouble();
    return fps > 0 && fps <= 60 ? p.canvas.frameRate : FrameRate(60, 1);
}
}  // namespace

PlaybackEngine::PlaybackEngine(std::filesystem::path projectDir, std::unique_ptr<audio::IAudioOutput> output,
                               Listener& listener)
    : dir_(std::move(projectDir)),
      listener_(listener),
      output_(output ? std::move(output) : audio::makeNullAudioOutput()),
      ring_(kRate * kChannels) {  // 1 s capacity
    audioThread_ = std::jthread([this](std::stop_token st) { audioLoop(st); });
    videoThread_ = std::jthread([this](std::stop_token st) { videoLoop(st); });
}

PlaybackEngine::~PlaybackEngine() {
    audioThread_.request_stop();
    videoThread_.request_stop();
    audioCv_.notify_all();
    videoCv_.notify_all();
    if (audioThread_.joinable()) audioThread_.join();
    if (videoThread_.joinable()) videoThread_.join();
    std::lock_guard lock(mutex_);
    stopDeviceLocked();
}

void PlaybackEngine::setProject(std::shared_ptr<const project::Project> project) {
    {
        std::lock_guard lock(mutex_);
        project_ = std::move(project);
        const Time end = durationLocked();
        if (!playing_ && anchor_ > end) anchor_ = end;
        renderRequested_ = true;
    }
    videoCv_.notify_all();
    audioCv_.notify_all();
}

void PlaybackEngine::setPreviewSize(QSize size) {
    {
        std::lock_guard lock(mutex_);
        const QSize even(std::max(16, size.width() & ~1), std::max(16, size.height() & ~1));
        if (even == previewSize_) return;
        previewSize_ = even;
        renderRequested_ = true;
    }
    videoCv_.notify_all();
}

void PlaybackEngine::refresh() {
    {
        std::lock_guard lock(mutex_);
        renderRequested_ = true;
    }
    videoCv_.notify_all();
}

Time PlaybackEngine::durationLocked() const { return project_ ? project_->timeline.duration() : Time::zero(); }

Time PlaybackEngine::duration() const {
    std::lock_guard lock(mutex_);
    return durationLocked();
}

bool PlaybackEngine::playing() const {
    std::lock_guard lock(mutex_);
    return playing_;
}

Time PlaybackEngine::position() const {
    std::lock_guard lock(mutex_);
    return positionLocked();
}

float PlaybackEngine::level(int channel) const {
    return levels_[std::clamp(channel, 0, 1)].load(std::memory_order_relaxed);
}

std::string PlaybackEngine::outputName() const {
    std::lock_guard lock(mutex_);
    return output_->deviceName();
}

Time PlaybackEngine::positionLocked() const {
    if (!playing_) return anchor_;
    const std::int64_t played = framesPlayed_.load(std::memory_order_acquire);
    Time t = anchor_ + Time::fromSamples(played, kRate) - latency_;
    if (played > 0) {
        // Between device callbacks, advance smoothly with the host clock. Once
        // everything has been handed to the device, keep going until the
        // speaker catches up (the latency), then the end is reached.
        const Time since = Time::fromNanoseconds(HostClock::now() - lastCallbackNs_.load(std::memory_order_acquire));
        const bool allHandedOver = anchor_ + Time::fromSamples(played, kRate) >= durationLocked();
        t += allHandedOver ? since : std::min(since, kMaxInterpolation);
    }
    return std::clamp(t, anchor_, std::max(anchor_, durationLocked()));
}

void PlaybackEngine::stopDeviceLocked() {
    if (deviceRunning_) {
        output_->stop();  // synchronous: render() no longer runs afterwards
        deviceRunning_ = false;
    }
}

void PlaybackEngine::restartLocked(Time from) {
    stopDeviceLocked();
    ring_.reset();
    framesPlayed_.store(0, std::memory_order_release);
    levels_[0].store(0);
    levels_[1].store(0);
    anchor_ = from;
    mixPosition_ = from.toSamples(kRate);
    endOfStream_.store(false, std::memory_order_release);
    ++generation_;
}

void PlaybackEngine::play() {
    {
        std::lock_guard lock(mutex_);
        if (playing_ || !project_) return;
        const Time end = durationLocked();
        if (end <= Time::zero()) return;
        Time from = anchor_;
        if (from >= end - previewRate(*project_).frameDuration()) from = Time::zero();  // replay from the start
        restartLocked(from);
        playing_ = true;
        playingFlag_.store(true, std::memory_order_release);
    }
    audioCv_.notify_all();
    videoCv_.notify_all();
    listener_.onPlayingChanged(true);
}

void PlaybackEngine::pause() {
    {
        std::lock_guard lock(mutex_);
        if (!playing_) return;
        anchor_ = positionLocked();
        playing_ = false;
        playingFlag_.store(false, std::memory_order_release);
        stopDeviceLocked();
        ++generation_;
        renderRequested_ = true;
    }
    videoCv_.notify_all();
    listener_.onPlayingChanged(false);
}

void PlaybackEngine::seek(Time t) {
    {
        std::lock_guard lock(mutex_);
        t = std::clamp(t, Time::zero(), durationLocked());
        if (playing_) {
            restartLocked(t);
        } else {
            anchor_ = t;
        }
        renderRequested_ = true;
    }
    audioCv_.notify_all();
    videoCv_.notify_all();
}

void PlaybackEngine::render(float* out, int frames) noexcept {
    const auto want = static_cast<std::size_t>(frames) * kChannels;
    const std::size_t got = ring_.read(out, want) & ~static_cast<std::size_t>(1);
    if (got < want) {
        std::memset(out + got, 0, (want - got) * sizeof(float));
        if (got == 0 && playingFlag_.load(std::memory_order_relaxed) && !endOfStream_.load(std::memory_order_relaxed)) {
            underruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    float peak[2] = {0, 0};
    for (std::size_t i = 0; i < got; i += 2) {
        peak[0] = std::max(peak[0], std::fabs(out[i]));
        peak[1] = std::max(peak[1], std::fabs(out[i + 1]));
    }
    for (int c = 0; c < 2; ++c) {
        // Fast attack, ~300 ms release for meters.
        const float decayed = levels_[c].load(std::memory_order_relaxed) * 0.93f;
        levels_[c].store(std::max(decayed, peak[c]), std::memory_order_relaxed);
    }
    framesPlayed_.fetch_add(static_cast<std::int64_t>(got / kChannels), std::memory_order_release);
    lastCallbackNs_.store(HostClock::now(), std::memory_order_release);
}

void PlaybackEngine::audioLoop(std::stop_token stop) {
    setCurrentThreadName("lectern.playback.audio");
    AudioMixer mixer(dir_);
    std::shared_ptr<const project::Project> current;
    std::vector<float> block(static_cast<std::size_t>(kMixBlock) * kChannels);

    while (!stop.stop_requested()) {
        std::shared_ptr<const project::Project> project;
        std::int64_t position = 0;
        std::uint64_t generation = 0;
        std::int64_t end = 0;
        std::size_t buffered = 0;
        {
            std::unique_lock lock(mutex_);
            audioCv_.wait_for(lock, stop, std::chrono::milliseconds(5), [this] { return playing_; });
            if (stop.stop_requested()) break;
            if (!playing_ || !project_) continue;
            project = project_;
            position = mixPosition_;
            generation = generation_;
            end = durationLocked().toSamples(kRate);
            buffered = (ring_.capacity() - ring_.writeAvailable()) / kChannels;
            // Start the device once enough is mixed (or nothing more will come).
            if (!deviceRunning_ && (buffered >= kPrefillFrames || position >= end)) {
                auto started = output_->start([this](float* out, int frames) { render(out, frames); });
                if (!started) {
                    LEC_WARN("editor", "audio output unavailable ({}); previewing without sound", started.error().message());
                    output_ = audio::makeNullAudioOutput();
                    started = output_->start([this](float* out, int frames) { render(out, frames); });
                }
                deviceRunning_ = static_cast<bool>(started);
                latency_ = output_->latency();
                lastCallbackNs_.store(HostClock::now(), std::memory_order_release);
            }
        }
        if (buffered >= kAheadFrames || position >= end) continue;
        if (project != current) {
            mixer.setProject(project);
            current = project;
        }
        const int frames = static_cast<int>(std::min<std::int64_t>({kMixBlock, end - position,
                                                                     static_cast<std::int64_t>(kAheadFrames - buffered)}));
        if (frames <= 0) continue;
        if (auto st = mixer.mix(position, frames, block.data()); !st) {
            LEC_WARN("editor", "preview audio: {}", st.error().message());
            std::memset(block.data(), 0, block.size() * sizeof(float));
        }
        std::lock_guard lock(mutex_);
        if (generation != generation_) continue;  // a seek or pause happened meanwhile
        ring_.write(block.data(), static_cast<std::size_t>(frames) * kChannels);
        mixPosition_ = position + frames;
        if (mixPosition_ >= end) endOfStream_.store(true, std::memory_order_release);
    }
}

void PlaybackEngine::setCompare(Compare mode, double split) {
    {
        std::lock_guard lock(mutex_);
        split = std::clamp(split, 0.0, 1.0);
        if (compare_ == mode && compareSplit_ == split) return;
        compare_ = mode;
        compareSplit_ = split;
        renderRequested_ = true;
    }
    videoCv_.notify_all();
}

void PlaybackEngine::setReference(QImage image) {
    {
        std::lock_guard lock(mutex_);
        reference_ = std::move(image);
        renderRequested_ = true;
    }
    videoCv_.notify_all();
}

void PlaybackEngine::setHighlight(timeline::ClipId clip, std::string nodeId) {
    {
        std::lock_guard lock(mutex_);
        if (highlightClip_ == clip && highlightNode_ == nodeId) return;
        highlightClip_ = clip;
        highlightNode_ = std::move(nodeId);
        renderRequested_ = true;
    }
    videoCv_.notify_all();
}

void PlaybackEngine::composeComparison(QImage& out, const QImage& before, const QImage& after, Compare mode, double split) {
    QPainter p(&out);
    if (mode == Compare::Wipe) {
        const int x = std::clamp(static_cast<int>(std::lround(split * out.width())), 0, out.width());
        p.drawImage(QRect(0, 0, x, out.height()), before, QRect(0, 0, x, before.height()));
        p.drawImage(QRect(x, 0, out.width() - x, out.height()), after, QRect(x, 0, after.width() - x, after.height()));
        return;
    }
    out.fill(Qt::black);  // side by side: two half-size pictures, centered vertically
    const int w = out.width() / 2;
    const int h = out.height() / 2;
    const int y = (out.height() - h) / 2;
    p.drawImage(QRect(0, y, w, h), before);
    p.drawImage(QRect(out.width() - w, y, w, h), after);
}

void PlaybackEngine::videoLoop(std::stop_token stop) {
    setCurrentThreadName("lectern.playback.video");
    FrameProvider frames(dir_, true);
    const std::unique_ptr<FrameRenderer> renderer = makeRenderer();  // created on this thread, used only here
    renderer->setProjectDirectory(dir_);
    std::shared_ptr<const project::Project> current;
    std::int64_t lastIndex = -1;
    QSize lastSize;

    while (!stop.stop_requested()) {
        std::shared_ptr<const project::Project> project;
        QSize size;
        bool requested = false;
        bool ended = false;
        Compare compare = Compare::Off;
        double split = 0.5;
        timeline::ClipId highlightClip;
        std::string highlightNode;
        QImage reference;
        Time t;
        {
            std::unique_lock lock(mutex_);
            const auto period = project_ ? previewRate(*project_).frameDuration() : Time::fromMilliseconds(33);
            const auto wait = std::chrono::microseconds(std::max<std::int64_t>(2000, period.toMicroseconds() / 2));
            videoCv_.wait_for(lock, stop, playing_ ? wait : std::chrono::microseconds(200'000),
                              [this] { return renderRequested_; });
            if (stop.stop_requested()) break;
            project = project_;
            size = previewSize_;
            compare = compare_;
            split = compareSplit_;
            highlightClip = highlightClip_;
            highlightNode = highlightNode_;
            reference = reference_;
            requested = renderRequested_;
            renderRequested_ = false;
            t = positionLocked();
            if (playing_ && project_ && t >= durationLocked()) {  // reached the end
                anchor_ = durationLocked();
                playing_ = false;
                playingFlag_.store(false, std::memory_order_release);
                stopDeviceLocked();
                ++generation_;
                ended = true;
                requested = true;
            }
        }
        if (ended) listener_.onPlayingChanged(false);
        if (!project) continue;
        if (project != current) {
            frames.setProject(project);
            current = project;
            requested = true;
        }
        const FrameRate rate = previewRate(*project);
        const std::int64_t index = rate.frameIndexAt(t);
        if (!requested && index == lastIndex && size == lastSize) continue;
        lastIndex = index;
        lastSize = size;
        // Exact frame times, like the exporter, so preview and export agree.
        const Time frameTime = std::min(rate.frameStart(index), std::max(Time::zero(), project->timeline.duration()));
        QImage image(size, QImage::Format_RGB32);
        RenderPlan plan = buildRenderPlan(*project, frameTime);
        if (!highlightNode.empty()) {
            if (const timeline::Clip* clip = project->timeline.findClip(highlightClip)) {
                const int node = enabledNodeIndex(clip->color, highlightNode);
                for (auto& l : plan.layers) {
                    if (l.clip == highlightClip) l.highlightNode = node;
                }
            }
        }
        const auto source = [&frames](const VisualLayer& l, QSizeF box) { return frames.image(l, box); };
        if (compare == Compare::Off) {
            renderer->render(plan, image, source);
        } else if (compare == Compare::Bypass) {
            renderer->render(ungraded(plan), image, source);
        } else {
            const QSize part = compare == Compare::SideBySide ? QSize(std::max(2, size.width() / 2), std::max(2, size.height() / 2)) : size;
            QImage before(part, QImage::Format_RGB32);
            QImage after(part, QImage::Format_RGB32);
            if (!reference.isNull()) {  // against a gallery still
                before = reference.scaled(part, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
            } else {
                renderer->render(ungraded(plan), before, source);
            }
            renderer->render(plan, after, source);
            composeComparison(image, before, after, compare, split);
        }
        framesRendered_.fetch_add(1, std::memory_order_relaxed);
        listener_.onFrame(image, t);
    }
}

}  // namespace lectern::editor
