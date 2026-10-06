#include "capture/RecordingStats.h"

namespace lectern::capture {

std::string_view toString(SessionState s) noexcept {
    switch (s) {
        case SessionState::Idle: return "idle";
        case SessionState::Recording: return "recording";
        case SessionState::Paused: return "paused";
        case SessionState::Stopping: return "stopping";
        case SessionState::Completed: return "completed";
        case SessionState::Failed: return "failed";
        case SessionState::Cancelled: return "cancelled";
    }
    return "idle";
}

std::optional<SessionState> sessionStateFromString(std::string_view s) noexcept {
    for (auto v : {SessionState::Idle, SessionState::Recording, SessionState::Paused, SessionState::Stopping,
                   SessionState::Completed, SessionState::Failed, SessionState::Cancelled}) {
        if (toString(v) == s) return v;
    }
    if (s == "recovered") return SessionState::Completed;
    return std::nullopt;
}

std::string_view toString(StopReason r) noexcept {
    switch (r) {
        case StopReason::None: return "none";
        case StopReason::User: return "user";
        case StopReason::DiskFull: return "disk-full";
        case StopReason::AllTracksFailed: return "all-tracks-failed";
        case StopReason::Error: return "error";
        case StopReason::Cancelled: return "cancelled";
    }
    return "none";
}

std::string_view toString(TrackState s) noexcept {
    switch (s) {
        case TrackState::Pending: return "pending";
        case TrackState::Recording: return "recording";
        case TrackState::Completed: return "completed";
        case TrackState::Failed: return "failed";
        case TrackState::Empty: return "empty";
        case TrackState::Recovered: return "recovered";
        case TrackState::Lost: return "lost";
    }
    return "pending";
}

std::optional<TrackState> trackStateFromString(std::string_view s) noexcept {
    for (auto v : {TrackState::Pending, TrackState::Recording, TrackState::Completed, TrackState::Failed,
                   TrackState::Empty, TrackState::Recovered, TrackState::Lost}) {
        if (toString(v) == s) return v;
    }
    return std::nullopt;
}

std::string_view toString(DiskLevel l) noexcept {
    switch (l) {
        case DiskLevel::Ok: return "ok";
        case DiskLevel::Warning: return "warning";
        case DiskLevel::Critical: return "critical";
    }
    return "ok";
}

}  // namespace lectern::capture
