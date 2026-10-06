#include "capture/RecoveryService.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"
#include "media/MediaProbe.h"
#include "media/SalvageRemux.h"

#include <algorithm>

namespace lectern::capture {

namespace {

bool isFinal(const SessionManifest& m) {
    return (m.state == SessionState::Completed || m.state == SessionState::Failed ||
            m.state == SessionState::Cancelled) &&
           !m.finishedAtUtc.empty();
}

// Rebuilds a minimal manifest from the media folders when session.json is
// missing or unreadable. Files are named "<trackId>-<last 8 of session id>.mkv".
SessionManifest reconstructManifest(const std::filesystem::path& projectDir, const std::string& sessionId) {
    SessionManifest m;
    m.sessionId = sessionId;
    m.title = "Recovered recording";
    m.hostClock = hostClockName();
    const std::string tag = sessionId.size() >= 8 ? sessionId.substr(sessionId.size() - 8) : sessionId;
    std::error_code ec;
    for (const char* folder : {"screen", "camera", "phone", "audio"}) {
        const auto dir = projectDir / "media" / folder;
        if (!std::filesystem::exists(dir, ec)) continue;
        for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
            const std::string name = de.path().filename().string();
            if (de.path().extension() != ".mkv" || name.find(tag) == std::string::npos) continue;
            ManifestTrack t;
            t.id = name.substr(0, name.find('-' + tag));
            if (std::string(folder) == "audio") {
                t.role = t.id.find("system") != std::string::npos ? TrackRole::SystemAudio : TrackRole::Microphone;
            } else {
                t.role = trackRoleFromString(folder).value_or(TrackRole::Screen);
            }
            t.mediaType = mediaTypeOf(t.role);
            t.file = std::string("media/") + folder + "/" + name;
            t.state = TrackState::Recording;
            m.tracks.push_back(std::move(t));
        }
    }
    return m;
}

void salvageTrack(const std::filesystem::path& projectDir, ManifestTrack& track, RecoveryReport& report) {
    const std::filesystem::path path = projectDir / track.file;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        track.state = TrackState::Lost;
        track.error = "file missing";
        ++report.tracksLost;
        report.notes.push_back(track.id + ": file missing");
        return;
    }
    const std::filesystem::path salvaged = path.string() + ".salvage.mkv";
    std::filesystem::remove(salvaged, ec);
    auto result = media::salvageRemux(path, salvaged);
    if (result) {
        // Verify before replacing the original.
        auto probe = media::probeMedia(salvaged);
        if (!probe) result = std::unexpected(probe.error());
    }
    if (!result) {
        std::filesystem::remove(salvaged, ec);
        track.state = TrackState::Lost;
        track.error = result.error().toString();
        ++report.tracksLost;
        report.notes.push_back(track.id + ": unrecoverable (" + result.error().message() + ")");
        return;
    }
    const std::filesystem::path partial = path.string() + ".partial";
    std::filesystem::rename(path, partial, ec);
    if (!ec) std::filesystem::rename(salvaged, path, ec);
    if (ec) {
        // Put the original back if the swap failed half-way.
        std::error_code ec2;
        if (!std::filesystem::exists(path, ec2)) std::filesystem::rename(partial, path, ec2);
        track.state = TrackState::Lost;
        track.error = "could not replace file: " + ec.message();
        ++report.tracksLost;
        return;
    }
    std::filesystem::remove(partial, ec);
    track.state = TrackState::Recovered;
    track.start = result->firstTimestamp;
    track.end = result->endTimestamp;
    track.bytes = result->bytesWritten;
    track.error.clear();
    ++report.tracksRecovered;
    report.notes.push_back(track.id + ": recovered " + (result->endTimestamp - result->firstTimestamp).toString() +
                           (result->inputWasTruncated ? " (truncated tail discarded)" : ""));
    LEC_INFO("recovery", "track {} recovered: {} packets, end {}", track.id, result->packetsCopied,
             result->endTimestamp.toString());
}

}  // namespace

Result<std::vector<ActiveSessionRegistry::Entry>> RecoveryService::findInterruptedSessions() const {
    auto entries = registry_.list();
    if (!entries) return fail(std::move(entries).error());
    std::vector<ActiveSessionRegistry::Entry> out;
    for (auto& e : *entries) {
        auto lock = fs::FileLock::tryAcquire(e.sessionDir / "session.lock");
        if (!lock) {
            if (lock.error().code() == ErrorCode::DeviceBusy) continue;  // still recording in another process
        }
        std::error_code ec;
        if (!std::filesystem::exists(e.sessionDir, ec)) {
            (void)registry_.remove(e.sessionId);  // project deleted by the user
            continue;
        }
        auto manifest = readManifest(e.sessionDir / "session.json");
        if (manifest && isFinal(*manifest)) {
            (void)registry_.remove(e.sessionId);  // finished; only deregistration was lost
            continue;
        }
        out.push_back(std::move(e));
    }
    return out;
}

Result<RecoveryReport> RecoveryService::recover(const ActiveSessionRegistry::Entry& entry) const {
    auto report = recoverSession(entry.projectDir, entry.sessionDir);
    if (report) (void)registry_.remove(entry.sessionId);
    return report;
}

Result<RecoveryReport> RecoveryService::recoverSession(const std::filesystem::path& projectDir,
                                                       const std::filesystem::path& sessionDir) {
    auto lock = fs::FileLock::tryAcquire(sessionDir / "session.lock");
    if (!lock) {
        if (lock.error().code() == ErrorCode::DeviceBusy) {
            return fail(ErrorCode::InvalidState, "session is still being recorded by another process");
        }
        return fail(std::move(lock).error());
    }

    RecoveryReport report;
    report.projectDir = projectDir;
    report.manifestPath = sessionDir / "session.json";
    const std::string sessionId = sessionDir.filename().string();

    auto manifest = readManifest(report.manifestPath);
    if (!manifest) {
        // The atomic writer may have left a complete .tmp if it crashed mid-rename.
        auto tmp = readManifest(report.manifestPath.string() + ".tmp");
        if (tmp) {
            manifest = std::move(tmp);
        } else {
            LEC_WARN("recovery", "manifest unreadable ({}); reconstructing from media files",
                     manifest.error().toString());
            manifest = reconstructManifest(projectDir, sessionId);
            report.notes.push_back("manifest was missing; tracks were rediscovered from media files");
        }
    }
    SessionManifest m = std::move(*manifest);
    if (m.sessionId.empty()) m.sessionId = sessionId;
    report.sessionId = m.sessionId;

    for (ManifestTrack& t : m.tracks) {
        if (t.state == TrackState::Completed || t.state == TrackState::Empty || t.state == TrackState::Recovered) {
            continue;
        }
        salvageTrack(projectDir, t, report);
    }

    Time end;
    for (const ManifestTrack& t : m.tracks) {
        if (t.end && *t.end > end && (t.state == TrackState::Completed || t.state == TrackState::Recovered)) end = *t.end;
    }
    m.duration = end;
    m.recovered = true;
    m.state = report.tracksRecovered > 0 || std::any_of(m.tracks.begin(), m.tracks.end(), [](const ManifestTrack& t) {
                  return t.state == TrackState::Completed;
              })
                  ? SessionState::Completed
                  : SessionState::Failed;
    if (m.stopReason == StopReason::None) m.stopReason = StopReason::Error;
    m.finishedAtUtc = json::utcNowIso8601();
    LEC_TRY(writeManifest(report.manifestPath, m));
    report.manifest = std::move(m);
    LEC_INFO("recovery", "session {}: {} track(s) recovered, {} lost, duration {}", report.sessionId,
             report.tracksRecovered, report.tracksLost, report.manifest.duration.toString());
    return report;
}

}  // namespace lectern::capture
