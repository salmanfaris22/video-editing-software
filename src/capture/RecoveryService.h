#pragma once

#include "capture/ActiveSessionRegistry.h"
#include "capture/SessionManifest.h"

#include <filesystem>
#include <string>
#include <vector>

namespace lectern::capture {

struct RecoveryReport {
    std::string sessionId;
    std::filesystem::path projectDir;
    std::filesystem::path manifestPath;
    SessionManifest manifest;
    std::vector<std::string> notes;  ///< human-readable per-track results
    int tracksRecovered = 0;
    int tracksLost = 0;
};

/// Finds recordings whose process died and salvages their media
/// (docs/RECORDING_ENGINE.md §8). Never touches a session whose lock is held
/// by a live process.
class RecoveryService {
public:
    explicit RecoveryService(std::filesystem::path registryDir = ActiveSessionRegistry::defaultDirectory())
        : registry_(std::move(registryDir)) {}

    /// Registry entries whose owner is gone and whose manifest is unfinished.
    /// Entries of sessions that finished cleanly but were not deregistered are
    /// cleaned up silently.
    [[nodiscard]] Result<std::vector<ActiveSessionRegistry::Entry>> findInterruptedSessions() const;

    /// Salvages the session and removes its registry entry on success.
    [[nodiscard]] Result<RecoveryReport> recover(const ActiveSessionRegistry::Entry& entry) const;

    /// Salvages a session directory directly (used by `lectern-rec --recover`).
    /// Works even if the manifest is missing or corrupt by rediscovering the
    /// track files from the media folders.
    [[nodiscard]] static Result<RecoveryReport> recoverSession(const std::filesystem::path& projectDir,
                                                               const std::filesystem::path& sessionDir);

private:
    ActiveSessionRegistry registry_;
};

}  // namespace lectern::capture
