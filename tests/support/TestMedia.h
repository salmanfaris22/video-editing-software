#pragma once

// Media files with machine-checkable content for reader, mixer, compositor
// and export tests.

#include "core/Error.h"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace lectern::test {

/// Solid luma level of frame `index` (distinct for 44 consecutive frames,
/// far enough apart to survive lossy coding).
[[nodiscard]] constexpr int frameLevel(std::int64_t index) noexcept { return 16 + static_cast<int>((index % 44) * 5); }

/// Sawtooth value of absolute sample `n` (media time × rate): identifies the
/// sample position exactly after lossless (FLAC) coding.
[[nodiscard]] constexpr float rampSample(std::int64_t n) noexcept {
    return (static_cast<float>(n % 1000) / 1000.0f - 0.5f) * 0.8f;
}

struct TestVideoSpec {
    double seconds = 3.0;
    int fps = 30;
    int width = 320;
    int height = 180;
    double startSeconds = 0.0;  ///< pts of the first frame (recordings start at session time)
    int gopFrames = 30;
    int u = 128;  ///< chroma, to tell sources apart by color
    int v = 128;
};

/// H.264 (software) in Matroska; frame i is solid luma frameLevel(i).
Status writeTestVideo(const std::filesystem::path& path, const TestVideoSpec& spec = {});

struct TestHdrVideoSpec {
    double seconds = 1.0;
    int fps = 30;
    int width = 320;
    int height = 180;
    /// "arib-std-b67" (HLG) or "smpte2084" (PQ); primaries are BT.2020.
    const char* transfer = "arib-std-b67";
    /// 10-bit luma code value (limited range 64…940) of every pixel; neutral chroma.
    int luma = 600;
};

/// 10-bit 4:2:0 FFV1 (lossless) in Matroska with BT.2020 + `transfer` color tags,
/// like an HDR phone recording.
Status writeTestHdrVideo(const std::filesystem::path& path, const TestHdrVideoSpec& spec = {});

struct TestAudioSpec {
    double seconds = 3.0;
    int sampleRate = 48'000;
    int channels = 1;
    double startSeconds = 0.0;
    float gain = 1.0f;  ///< multiplies the ramp (0 = digital silence)
    /// Optional loudness over media time (seconds), multiplied with `gain`
    /// (e.g. speech with pauses for silence detection).
    std::function<float(double)> envelope;
};

/// FLAC in Matroska; sample n (from media time 0) is gain × rampSample(n).
Status writeTestAudio(const std::filesystem::path& path, const TestAudioSpec& spec = {});

}  // namespace lectern::test
