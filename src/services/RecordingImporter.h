#pragma once

#include "capture/RecoveryService.h"
#include "capture/SessionManifest.h"
#include "project/Project.h"

#include <filesystem>
#include <string>
#include <vector>

namespace lectern::services {

struct ImportOptions {
    bool probeMedia = true;           ///< read stream metadata from the files
    bool computeFingerprints = true;  ///< for relinking moved media
};

/// Builds a project from a finished (or recovered) recording session
/// (docs/TIMELINE_ENGINE.md §3): one MediaSource and one clip per track,
/// placed at the track's first timestamp so sync is preserved, all clips in
/// one link group, pauses as markers, and a default layout region.
[[nodiscard]] Result<project::Project> buildProjectFromSession(const capture::SessionManifest& manifest,
                                                               const std::filesystem::path& projectDir,
                                                               const ImportOptions& options = {});

/// buildProjectFromSession + save project.json in `projectDir`.
[[nodiscard]] Result<project::Project> importRecording(const std::filesystem::path& projectDir,
                                                       const capture::SessionManifest& manifest,
                                                       const ImportOptions& options = {});

struct RecoveredProject {
    std::filesystem::path projectDir;
    capture::RecoveryReport report;
    std::optional<project::Project> project;  ///< empty if nothing could be recovered
};

/// Finds interrupted recordings, salvages them and builds their projects.
[[nodiscard]] Result<std::vector<RecoveredProject>> recoverInterruptedRecordings(
    const std::filesystem::path& registryDir = capture::ActiveSessionRegistry::defaultDirectory());

}  // namespace lectern::services
