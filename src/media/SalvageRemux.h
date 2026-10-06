#pragma once

#include "core/Error.h"
#include "core/Time.h"

#include <cstdint>
#include <filesystem>

namespace lectern::media {

struct SalvageResult {
    std::int64_t packetsCopied = 0;
    std::int64_t packetsDropped = 0;  ///< corrupt / non-monotonic packets skipped
    Time firstTimestamp;
    Time endTimestamp;                ///< last packet pts + duration
    std::uint64_t bytesWritten = 0;
    bool inputWasTruncated = false;   ///< stopped at a read error instead of clean EOF
};

/// Recovers a crash-truncated recording by stream-copying every intact packet
/// into a fresh, properly finalized Matroska file (rebuilding Cues and
/// Duration). Stops at the first unreadable data. Timestamps are copied
/// unchanged, so each track's offset on the session timeline is preserved.
[[nodiscard]] Result<SalvageResult> salvageRemux(const std::filesystem::path& input,
                                                 const std::filesystem::path& output);

}  // namespace lectern::media
