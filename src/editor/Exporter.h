#pragma once

// Renders the edited timeline to a shareable file (docs/RENDERING_PIPELINE.md
// §7): the same render plan, compositor and mixer as the preview, offline.

#include "project/Project.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace lectern::editor {

struct ExportOptions {
    std::filesystem::path output;  ///< .mp4, .mov or .mkv — H.264 + AAC
    int width = 1920;              ///< even; a canvas of another aspect is letterboxed
    int height = 1080;
    FrameRate frameRate{30, 1};
    /// "draft", "standard", "high", "max" or "ultra"
    std::string quality = "high";
    double bitrateScale = 1.0;  ///< multiplies the preset bit rate (0.25…2)
    /// "mp4", "mkv" or "mov"; when empty, taken from `output`'s extension
    std::string container;
    bool hardwareEncoder = true;
    std::optional<TimeRange> range;  ///< default: the whole timeline
};

struct ExportProgress {
    double fraction = 0;  ///< 0…1
    Time position;        ///< timeline time rendered so far
    double speed = 0;     ///< × real time
};

struct ExportResult {
    std::filesystem::path path;
    Time duration;
    std::int64_t frames = 0;
    std::uint64_t bytes = 0;
    std::string videoEncoder;
    bool hardware = false;
    double seconds = 0;  ///< wall-clock time taken
};

/// Video bit rate for an export (H.264), by size, rate, quality and scale.
[[nodiscard]] std::int64_t exportBitrate(int width, int height, FrameRate fps, const std::string& quality,
                                         double bitrateScale = 1.0);

/// Writes the export next to its destination and renames it into place on
/// success; a failed or cancelled export leaves nothing behind. `progress`
/// is called from the exporting thread; `cancel` stops with ErrorCode::Cancelled.
[[nodiscard]] Result<ExportResult> exportProject(const project::Project& project, const std::filesystem::path& projectDir,
                                                 const ExportOptions& options,
                                                 const std::function<void(const ExportProgress&)>& progress = {},
                                                 const std::atomic<bool>* cancel = nullptr);

}  // namespace lectern::editor
