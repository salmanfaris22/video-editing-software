#pragma once

#include "capture/RecordingStats.h"
#include "core/Error.h"
#include "core/Json.h"
#include "core/Time.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lectern::capture {

/// One recorded track as described in `session.json`.
struct ManifestTrack {
    std::string id;
    TrackRole role = TrackRole::Screen;
    MediaType mediaType = MediaType::Video;
    std::string file;  ///< relative to the project directory
    std::string sourceName;
    std::string deviceId;
    std::string codec;
    std::string encoder;
    bool hardwareEncoder = false;
    int width = 0;
    int height = 0;
    FrameRate frameRate;
    int sampleRate = 0;
    int channels = 0;
    TrackState state = TrackState::Pending;
    std::optional<Time> start;  ///< session time of first frame/sample in the file
    std::optional<Time> end;
    std::uint64_t bytes = 0;
    std::uint64_t framesEncoded = 0;
    std::uint64_t framesDuplicated = 0;
    std::uint64_t framesDropped = 0;
    double driftPpm = 0;
    std::uint64_t hardCorrections = 0;
    std::int64_t latencyCompensationNs = 0;
    std::string error;
};

struct ManifestPause {
    std::int64_t hostBeginNs = 0;
    std::optional<std::int64_t> hostEndNs;
    Time sessionTime;
};

/// Recording session manifest (docs/RECORDING_ENGINE.md §8). Rewritten
/// atomically on every state change and periodically as a checkpoint; it is
/// the input for both project creation and crash recovery.
struct SessionManifest {
    static constexpr int kFormatVersion = 1;

    int formatVersion = kFormatVersion;
    std::string sessionId;
    std::string title;
    SessionState state = SessionState::Idle;
    bool recovered = false;
    StopReason stopReason = StopReason::None;
    std::string createdAtUtc;
    std::string finishedAtUtc;
    std::string appVersion;
    std::string hostClock;  ///< "mach_absolute_time" | "qpc" | "clock_monotonic"
    std::int64_t startHostNs = 0;
    std::optional<std::int64_t> stopHostNs;
    Time duration;  ///< recorded session duration (excludes pauses)
    std::int64_t checkpointHostNs = 0;
    std::vector<ManifestPause> pauses;
    std::vector<ManifestTrack> tracks;
};

[[nodiscard]] json::Json toJson(const SessionManifest& manifest);
[[nodiscard]] Result<SessionManifest> manifestFromJson(const json::Json& value);
Status writeManifest(const std::filesystem::path& path, const SessionManifest& manifest);
[[nodiscard]] Result<SessionManifest> readManifest(const std::filesystem::path& path);
[[nodiscard]] std::string hostClockName();

}  // namespace lectern::capture
