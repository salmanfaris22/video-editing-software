#pragma once

#include "core/Error.h"

#include <filesystem>
#include <string>
#include <vector>

namespace lectern::test {

/// Unique temporary directory removed on destruction (kept if the
/// LECTERN_KEEP_TEST_FILES environment variable is set, for debugging).
class TempDir {
public:
    explicit TempDir(const std::string& prefix = "lectern-test");
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

struct DecodedVideo {
    int width = 0;
    int height = 0;
    std::vector<double> ptsSeconds;  ///< per decoded frame
    std::vector<double> meanLuma;    ///< 0..255 per frame
    std::vector<bool> keyframes;
};

struct DecodedAudio {
    int sampleRate = 0;
    int channels = 0;
    double startSeconds = 0;     ///< pts of the first decoded sample
    std::vector<float> channel0; ///< samples of the first channel
};

/// Decodes the whole stream, or only [startSeconds, startSeconds + durationSeconds)
/// when startSeconds >= 0 (seeks first; used for long recordings).
Result<DecodedVideo> decodeVideo(const std::filesystem::path& path, double startSeconds = -1,
                                 double durationSeconds = -1);
Result<DecodedAudio> decodeAudio(const std::filesystem::path& path, double startSeconds = -1,
                                 double durationSeconds = -1);

/// Resident memory of this process in bytes (0 if unavailable).
std::uint64_t currentRssBytes();

/// Start times (seconds) of runs where the predicate holds, e.g. flash frames.
std::vector<double> risingEdges(const std::vector<double>& times, const std::vector<bool>& flags);
/// Start times of loud bursts (|x| > threshold) separated by at least minGapSeconds.
std::vector<double> burstStarts(const DecodedAudio& audio, float threshold, double minGapSeconds);

}  // namespace lectern::test
