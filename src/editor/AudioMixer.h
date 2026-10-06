#pragma once

// Timeline audio → interleaved stereo float at 48 kHz (docs/RENDERING_PIPELINE.md
// §6). The same mixer feeds playback and export, so what you hear is what
// is exported.

#include "media/AudioReader.h"
#include "project/Project.h"

#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace lectern::editor {

class AudioMixer {
public:
    static constexpr int kSampleRate = 48'000;
    static constexpr int kChannels = 2;

    explicit AudioMixer(std::filesystem::path projectDir);
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    /// Swaps in a new document snapshot (after an edit); readers are kept.
    void setProject(std::shared_ptr<const project::Project> project);

    /// Mixes timeline samples [sample, sample + frames) at 48 kHz into `out`
    /// (2 × frames floats, overwritten). Applies track gain/mute/solo, clip
    /// gain, volume automation and fades; clamps to [−1, 1].
    Status mix(std::int64_t sample, int frames, float* out);
    Status mix(Time t, int frames, float* out) { return mix(t.toSamples(kSampleRate), frames, out); }

    /// Peak level per channel of the last mix call (meters).
    [[nodiscard]] float peakLeft() const noexcept { return peak_[0]; }
    [[nodiscard]] float peakRight() const noexcept { return peak_[1]; }
    /// Clips whose media could not be opened (shown as warnings).
    [[nodiscard]] std::vector<std::string> missingMedia() const;

private:
    struct Source {
        std::unique_ptr<media::AudioReader> reader;
        std::uint64_t lastUse = 0;
        bool failed = false;
        std::string name;
    };
    media::AudioReader* readerFor(const project::MediaSource& media);

    std::filesystem::path dir_;
    std::shared_ptr<const project::Project> project_;
    std::map<project::MediaId, Source> sources_;
    std::vector<float> scratch_;
    std::uint64_t calls_ = 0;
    float peak_[2] = {0, 0};
};

/// Whether a media source carries audio the mixer can play.
[[nodiscard]] bool hasAudio(const project::MediaSource& media);
[[nodiscard]] double dbToGain(double db) noexcept;

}  // namespace lectern::editor
