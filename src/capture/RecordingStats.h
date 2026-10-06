#pragma once

#include "audio/LevelMeter.h"
#include "capture/CaptureTypes.h"
#include "core/Time.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lectern::capture {

enum class SessionState { Idle, Recording, Paused, Stopping, Completed, Failed, Cancelled };
enum class StopReason { None, User, DiskFull, AllTracksFailed, Error, Cancelled };
enum class TrackState { Pending, Recording, Completed, Failed, Empty, Recovered, Lost };
enum class DiskLevel { Ok, Warning, Critical };

[[nodiscard]] std::string_view toString(SessionState s) noexcept;
[[nodiscard]] std::optional<SessionState> sessionStateFromString(std::string_view s) noexcept;
[[nodiscard]] std::string_view toString(StopReason r) noexcept;
[[nodiscard]] std::string_view toString(TrackState s) noexcept;
[[nodiscard]] std::optional<TrackState> trackStateFromString(std::string_view s) noexcept;
[[nodiscard]] std::string_view toString(DiskLevel l) noexcept;

struct VideoTrackStats {
    std::uint64_t framesReceived = 0;    ///< frames handed to the writer by the source
    std::uint64_t framesEncoded = 0;     ///< output slots encoded (real + duplicates)
    std::uint64_t framesDuplicated = 0;  ///< CFR slots filled by repeating a frame
    std::uint64_t framesDecimated = 0;   ///< extra frames within one slot (source faster than output)
    std::uint64_t framesLate = 0;
    std::uint64_t droppedQueueFull = 0;  ///< encoder overloaded
    std::uint64_t droppedBySource = 0;   ///< reported by the OS / device
    std::uint64_t gapSlots = 0;          ///< stalled source beyond maxHold
    std::size_t inputQueueDepth = 0;
    std::string encoder;
    bool hardwareEncoder = false;
    bool zeroCopy = false;
    int width = 0;
    int height = 0;
};

struct AudioTrackStats {
    std::uint64_t framesIn = 0;
    std::uint64_t framesOut = 0;
    std::uint64_t silenceInserted = 0;  ///< output frames of silence added by hard corrections / padding
    std::uint64_t inputDropped = 0;     ///< input frames dropped by hard corrections
    std::uint64_t hardCorrections = 0;
    std::uint64_t overflowFrames = 0;   ///< RT ring overflow (from the live source)
    std::uint64_t discontinuities = 0;
    double driftPpm = 0;
    double lastErrorMs = 0;
    audio::LevelSnapshot levels;
    bool active = false;                ///< audio arrived in the last second
    int sampleRate = 0;
    int channels = 0;
};

struct TrackStats {
    std::string id;
    TrackRole role = TrackRole::Screen;
    MediaType type = MediaType::Video;
    TrackState state = TrackState::Pending;
    std::string sourceName;
    std::uint64_t bytesWritten = 0;
    std::size_t packetQueueBytes = 0;
    std::optional<VideoTrackStats> video;
    std::optional<AudioTrackStats> audio;
    std::string error;
};

struct RecordingStats {
    SessionState state = SessionState::Idle;
    Time duration;
    int pauseCount = 0;
    DiskLevel diskLevel = DiskLevel::Ok;
    std::uint64_t diskAvailableBytes = 0;
    double writeBytesPerSecond = 0;
    double diskSecondsRemaining = 0;  ///< < 0 = unknown/infinite
    std::uint64_t totalBytes = 0;
    std::vector<TrackStats> tracks;

    /// Frames lost by the pipeline or device (excludes expected duplicates).
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept {
        std::uint64_t n = 0;
        for (const auto& t : tracks) {
            if (t.video) n += t.video->droppedQueueFull + t.video->droppedBySource;
        }
        return n;
    }
};

}  // namespace lectern::capture
