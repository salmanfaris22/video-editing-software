#include "editor/SilenceDetector.h"

#include "editor/AudioMixer.h"

#include <cmath>

namespace lectern::editor {

Result<std::vector<TimeRange>> detectSilences(const project::Project& source, const std::filesystem::path& projectDir,
                                              const SilenceOptions& options, const std::function<void(double)>& progress,
                                              const std::atomic<bool>* cancel) {
    // Analyse the voice only: mute every audio track without microphone media
    // (music would otherwise hide every pause), unless there is no voice.
    auto project = std::make_shared<project::Project>(source);
    bool haveVoice = false;
    for (auto& track : project->timeline.tracks) {
        bool voice = false;
        for (const auto& clip : track.clips) {
            const auto* m = project->findMedia(clip.media);
            voice = voice || (m && m->role == project::MediaRole::Microphone);
        }
        haveVoice = haveVoice || voice;
        track.solo = voice;
        track.muted = false;
        track.gainDb = 0;
    }
    if (!haveVoice) {
        for (auto& track : project->timeline.tracks) track.solo = false;
    }

    AudioMixer mixer(projectDir);
    mixer.setProject(project);
    const Time duration = project->timeline.duration();
    const int window = static_cast<int>(std::max<std::int64_t>(1, options.window.toSamples(AudioMixer::kSampleRate)));
    const std::int64_t total = duration.toSamples(AudioMixer::kSampleRate);
    const double threshold = std::pow(10.0, options.thresholdDb / 20.0);
    std::vector<float> buffer(static_cast<std::size_t>(window) * AudioMixer::kChannels);

    std::vector<TimeRange> silences;
    std::int64_t silentFrom = -1;
    auto close = [&](std::int64_t end) {
        if (silentFrom < 0) return;
        const Time start = Time::fromSamples(silentFrom, AudioMixer::kSampleRate);
        const Time stop = Time::fromSamples(end, AudioMixer::kSampleRate);
        silentFrom = -1;
        if (stop - start < options.minSilence) return;
        // Keep `padding` next to speech; the timeline's very start and end need none.
        const Time from = start.isZero() ? start : start + options.padding;
        const Time to = end >= total ? stop : stop - options.padding;
        if (to - from >= Time::fromMilliseconds(100)) silences.push_back(TimeRange::fromStartEnd(from, to));
    };

    for (std::int64_t pos = 0; pos < total; pos += window) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return fail(ErrorCode::Cancelled, "cancelled");
        const int n = static_cast<int>(std::min<std::int64_t>(window, total - pos));
        LEC_TRY(mixer.mix(pos, n, buffer.data()));
        double sum = 0;
        for (int i = 0; i < n * AudioMixer::kChannels; ++i) sum += static_cast<double>(buffer[i]) * buffer[i];
        const double rms = std::sqrt(sum / (n * AudioMixer::kChannels));
        if (rms < threshold) {
            if (silentFrom < 0) silentFrom = pos;
        } else {
            close(pos);
        }
        if (progress && (pos / window) % 250 == 0) progress(static_cast<double>(pos) / static_cast<double>(total));
    }
    close(total);
    if (progress) progress(1.0);
    return silences;
}

}  // namespace lectern::editor
