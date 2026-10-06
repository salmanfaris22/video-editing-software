#include "editor/AudioMixer.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lectern::editor {

namespace {
constexpr std::uint64_t kIdleCalls = 2000;  // ~40 s of 1024-frame blocks: close unused readers
}  // namespace

double dbToGain(double db) noexcept { return db <= -96.0 ? 0.0 : std::pow(10.0, db / 20.0); }

bool hasAudio(const project::MediaSource& media) {
    return media.kind == project::MediaKind::Audio || (media.kind == project::MediaKind::Video && media.info.audio);
}

AudioMixer::AudioMixer(std::filesystem::path projectDir) : dir_(std::move(projectDir)) {}
AudioMixer::~AudioMixer() = default;

void AudioMixer::setProject(std::shared_ptr<const project::Project> project) { project_ = std::move(project); }

std::vector<std::string> AudioMixer::missingMedia() const {
    std::vector<std::string> out;
    for (const auto& [id, s] : sources_) {
        if (s.failed) out.push_back(s.name);
    }
    return out;
}

media::AudioReader* AudioMixer::readerFor(const project::MediaSource& media) {
    Source& s = sources_[media.id];
    s.lastUse = calls_;
    if (s.reader) return s.reader.get();
    if (s.failed) return nullptr;
    s.name = media.name;
    auto reader = media::AudioReader::open(dir_ / media.path, kSampleRate);
    if (!reader) {
        LEC_WARN("editor", "audio of '{}' unavailable: {}", media.name, reader.error().message());
        s.failed = true;
        return nullptr;
    }
    s.reader = std::move(*reader);
    return s.reader.get();
}

Status AudioMixer::mix(std::int64_t sample, int frames, float* out) {
    ++calls_;
    std::memset(out, 0, static_cast<std::size_t>(frames) * kChannels * sizeof(float));
    peak_[0] = peak_[1] = 0;
    if (!project_ || frames <= 0) return ok();
    const project::Project& p = *project_;
    if (scratch_.size() < static_cast<std::size_t>(frames) * kChannels) scratch_.resize(static_cast<std::size_t>(frames) * kChannels);

    const Time blockStart = Time::fromSamples(sample, kSampleRate);
    const Time blockEnd = Time::fromSamples(sample + frames, kSampleRate);
    const bool anySolo = std::any_of(p.timeline.tracks.begin(), p.timeline.tracks.end(),
                                     [](const timeline::Track& t) { return t.solo; });

    for (const timeline::Track& track : p.timeline.tracks) {
        if (track.muted || (anySolo && !track.solo)) continue;
        if (track.kind == timeline::TrackKind::Subtitle) continue;
        const double trackGain = dbToGain(track.gainDb);
        // First clip that ends after the block start.
        auto it = std::upper_bound(track.clips.begin(), track.clips.end(), blockStart,
                                   [](Time t, const timeline::Clip& c) { return t < c.range.end(); });
        for (; it != track.clips.end() && it->range.start < blockEnd; ++it) {
            const timeline::Clip& clip = *it;
            if (!clip.enabled || clip.audio.muted || clip.kind != timeline::ClipKind::Media) continue;
            if (clip.speed != Rational(1, 1)) continue;  // retimed clips play silently (pitch-correct retiming: planned)
            const project::MediaSource* media = p.findMedia(clip.media);
            if (!media || !hasAudio(*media)) continue;

            const std::int64_t clipFirst = clip.range.start.toSamples(kSampleRate);
            const std::int64_t clipLast = clip.range.end().toSamples(kSampleRate);
            const std::int64_t from = std::max(sample, clipFirst);
            const std::int64_t to = std::min(sample + frames, clipLast);
            if (to <= from) continue;
            media::AudioReader* reader = readerFor(*media);
            if (!reader) continue;
            const int n = static_cast<int>(to - from);
            const Time at = Time::fromSamples(from, kSampleRate);
            LEC_TRY(reader->read(clip.sourceTimeAt(at), n, scratch_.data()));

            // Gain envelope: volume automation × fades, linear across the block.
            const double base = trackGain * dbToGain(clip.audio.gainDb);
            auto gainAt = [&](std::int64_t s) {
                const Time local = Time::fromSamples(s - clipFirst, kSampleRate);
                double g = base * std::max(0.0, clip.audio.volume.evaluate(local));
                const Time fadeIn = clip.audio.fadeIn;
                const Time fadeOut = clip.audio.fadeOut;
                if (fadeIn > Time::zero() && local < fadeIn) g *= local.toSecondsF() / fadeIn.toSecondsF();
                const Time remaining = clip.range.duration - local;
                if (fadeOut > Time::zero() && remaining < fadeOut) {
                    g *= std::max(0.0, remaining.toSecondsF()) / fadeOut.toSecondsF();
                }
                return g;
            };
            const double g0 = gainAt(from);
            const double g1 = gainAt(to);
            float* dst = out + (from - sample) * kChannels;
            const float* src = scratch_.data();
            if (g0 == g1) {
                const auto g = static_cast<float>(g0);
                if (g == 0.0f) continue;
                for (int i = 0; i < n * kChannels; ++i) dst[i] += src[i] * g;
            } else {
                const double step = (g1 - g0) / n;
                for (int i = 0; i < n; ++i) {
                    const auto g = static_cast<float>(g0 + step * i);
                    dst[2 * i] += src[2 * i] * g;
                    dst[2 * i + 1] += src[2 * i + 1] * g;
                }
            }
        }
    }

    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < kChannels; ++c) {
            float& v = out[i * kChannels + c];
            v = std::clamp(v, -1.0f, 1.0f);
            peak_[c] = std::max(peak_[c], std::fabs(v));
        }
    }
    // Close readers nobody used for a while (deleted clips, scrolled-away media).
    std::erase_if(sources_, [this](const auto& entry) {
        return !entry.second.failed && calls_ - entry.second.lastUse > kIdleCalls;
    });
    return ok();
}

}  // namespace lectern::editor
