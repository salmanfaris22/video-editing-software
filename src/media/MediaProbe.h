#pragma once

#include "core/Error.h"
#include "core/Json.h"
#include "media/MediaInfo.h"

#include <filesystem>

namespace lectern::media {

struct ProbeOptions {
    /// Bytes/time FFmpeg may read to detect stream parameters. Small values
    /// keep import fast for long files; recorded Matroska needs very little.
    std::int64_t probeSizeBytes = 8 << 20;
    std::int64_t analyzeDurationUs = 5'000'000;
};

/// Reads container and stream metadata without decoding frames.
/// Never throws; corrupt or unsupported files return ErrorCode::Corrupt /
/// ErrorCode::Unsupported with FFmpeg's reason.
[[nodiscard]] Result<MediaInfo> probeMedia(const std::filesystem::path& path, const ProbeOptions& options = {});

/// JSON form used by lectern-probe and logs.
[[nodiscard]] json::Json toJson(const MediaInfo& info);

}  // namespace lectern::media
