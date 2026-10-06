#pragma once

// Audio playback device abstraction (editor preview). Platform backends
// implement it (CoreAudio, WASAPI); NullAudioOutput keeps time without sound.

#include "core/Error.h"
#include "core/Time.h"

#include <functional>
#include <memory>
#include <string>

namespace lectern::audio {

class IAudioOutput {
public:
    static constexpr int kSampleRate = 48'000;
    static constexpr int kChannels = 2;

    /// Fills `frames` interleaved stereo float frames. Runs on a real-time
    /// thread: no locks, allocation, logging or I/O.
    using RenderCallback = std::function<void(float* interleaved, int frames)>;

    virtual ~IAudioOutput() = default;
    /// Starts pulling audio; the device converts 48 kHz stereo float to its format.
    virtual Status start(RenderCallback render) = 0;
    /// Stops; when it returns, `render` is no longer called.
    virtual void stop() = 0;
    /// Time from render() to the speaker (device latency + buffering).
    [[nodiscard]] virtual Time latency() const = 0;
    [[nodiscard]] virtual std::string deviceName() const = 0;
};

/// Calls `render` in real time (10 ms blocks) and discards the audio: the
/// clock for machines without an output device, and for tests.
[[nodiscard]] std::unique_ptr<IAudioOutput> makeNullAudioOutput();

}  // namespace lectern::audio
