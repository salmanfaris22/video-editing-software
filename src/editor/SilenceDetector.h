#pragma once

// Finds pauses in the narration for "Remove silences" (Cut panel).

#include "project/Project.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <vector>

namespace lectern::editor {

struct SilenceOptions {
    double thresholdDb = -42.0;                       ///< louder than this counts as speech
    Time minSilence = Time::fromMilliseconds(700);    ///< shorter pauses are kept
    Time padding = Time::fromMilliseconds(150);       ///< breathing room left around speech
    Time window = Time::fromMilliseconds(20);         ///< analysis window
};

/// Timeline ranges where the voice tracks (microphone media; all audio when
/// there is none) stay below the threshold. The returned ranges already
/// exclude `padding` around speech and are ready for edit::removeRanges.
/// `progress` receives 0…1; `cancel` aborts with ErrorCode::Cancelled.
[[nodiscard]] Result<std::vector<TimeRange>> detectSilences(const project::Project& project,
                                                            const std::filesystem::path& projectDir,
                                                            const SilenceOptions& options = {},
                                                            const std::function<void(double)>& progress = {},
                                                            const std::atomic<bool>* cancel = nullptr);

}  // namespace lectern::editor
