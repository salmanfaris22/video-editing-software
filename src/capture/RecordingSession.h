#pragma once

#include "capture/ActiveSessionRegistry.h"
#include "capture/DiskSpaceMonitor.h"
#include "capture/LiveSources.h"
#include "capture/RecordingStats.h"
#include "capture/SessionClock.h"
#include "capture/InputEventRecorder.h"
#include "capture/SessionManifest.h"
#include "capture/TrackWriters.h"
#include "core/Clock.h"
#include "core/FileSystem.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace lectern::capture {

/// One source to record. Exactly one of `videoSource` / `audioSource` is set,
/// matching the role's media type. Output paths are assigned by the session.
struct TrackPlan {
    std::string trackId;  ///< unique in the session, e.g. "screen", "camera", "microphone"
    TrackRole role = TrackRole::Screen;
    std::string displayName;
    std::shared_ptr<LiveVideoSource> videoSource;
    std::shared_ptr<LiveAudioSource> audioSource;
    VideoTrackSettings video;
    AudioTrackSettings audio;
};

struct SessionConfig {
    std::string sessionId;  ///< empty → UUIDv7
    std::string title;
    std::filesystem::path projectDir;   ///< media/ and recordings/ are created inside
    std::filesystem::path registryDir;  ///< empty → ActiveSessionRegistry::defaultDirectory()
    std::vector<TrackPlan> tracks;
    DiskSpacePolicy disk;
    DiskSpaceMonitor::Query diskQuery;  ///< empty → real file system
    std::chrono::milliseconds monitorInterval{250};
    std::chrono::milliseconds diskInterval{2'000};
    std::chrono::milliseconds checkpointInterval{5'000};
    /// Real-time cap on waiting for audio to reach the stop instant.
    std::chrono::milliseconds maxAudioDrainWait{3'000};
    const IClock* clock = nullptr;  ///< null → HostClock
};

struct SessionResult {
    SessionState state = SessionState::Idle;
    StopReason reason = StopReason::None;
    SessionManifest manifest;
    std::filesystem::path manifestPath;
    std::filesystem::path projectDir;
};

/// Notifications from engine threads (monitor/finalizer). Implementations
/// must be thread-safe and must not call back into the session synchronously
/// from onFinished.
class IRecordingObserver {
public:
    virtual ~IRecordingObserver() = default;
    virtual void onStateChanged(SessionState /*state*/) {}
    virtual void onWarning(const std::string& /*code*/, const std::string& /*message*/) {}
    virtual void onTrackFailed(const std::string& /*trackId*/, const Error& /*error*/) {}
    virtual void onFinished(const SessionResult& /*result*/) {}
};

/// One recording (docs/RECORDING_ENGINE.md §2): owns the session clock, one
/// writer per track, the manifest, the crash-detection lock and registry
/// entry, and the monitor thread (stats, disk space, checkpoints).
class RecordingSession {
public:
    static Result<std::unique_ptr<RecordingSession>> create(SessionConfig config,
                                                            IRecordingObserver* observer = nullptr);
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    /// Creates files, attaches writers to the live sources and sets T0.
    Status start();
    Status pause();
    Status resume();
    /// Initiates a graceful stop (non-blocking). Completion is reported via
    /// IRecordingObserver::onFinished and waitForCompletion().
    Status stop(StopReason reason = StopReason::User);
    Result<SessionResult> waitForCompletion(std::chrono::milliseconds timeout);
    /// Discards the recording: stops everything and deletes its files.
    void cancel();

    [[nodiscard]] SessionState state() const;
    [[nodiscard]] RecordingStats stats() const;
    [[nodiscard]] SessionManifest manifestSnapshot() const;
    [[nodiscard]] const std::string& sessionId() const noexcept { return config_.sessionId; }
    [[nodiscard]] const std::filesystem::path& projectDir() const noexcept { return config_.projectDir; }
    [[nodiscard]] std::filesystem::path sessionDir() const;
    [[nodiscard]] std::filesystem::path manifestPath() const;
    [[nodiscard]] const SessionClock& clock() const noexcept { return clock_; }

private:
    struct Track {
        TrackPlan plan;
        std::string relativeFile;
        std::unique_ptr<VideoTrackWriter> video;
        std::unique_ptr<AudioTrackWriter> audio;
        bool failureReported = false;
    };

    RecordingSession(SessionConfig config, IRecordingObserver* observer);
    void setState(SessionState state);
    void notifyState(SessionState state);
    void monitorLoop(std::stop_token stopToken);
    void finalize();
    void writeCheckpoint();
    SessionManifest buildManifest(bool final) const;
    std::uint64_t totalBytes() const;
    void releaseResources();

    SessionConfig config_;
    IRecordingObserver* observer_;
    const IClock& hostClock_;
    SessionClock clock_;
    ActiveSessionRegistry registry_;
    DiskSpaceMonitor disk_;
    std::vector<Track> tracks_;
    fs::FileLock lock_;

    mutable std::mutex mutex_;  // guards state_, stopReason_, result_, diskStatus_, createdAt_
    std::condition_variable cv_;
    SessionState state_ = SessionState::Idle;
    StopReason stopReason_ = StopReason::None;
    std::optional<SessionResult> result_;
    DiskSpaceMonitor::Status diskStatus_;
    std::string createdAt_;
    bool checkpointRequested_ = false;
    bool tracksReady_ = false;

    std::mutex manifestWriteMutex_;
    std::jthread monitor_;
    std::jthread finalizer_;
    std::unique_ptr<InputEventRecorder> inputRecorder_;
};

}  // namespace lectern::capture
