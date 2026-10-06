#pragma once

#include "capture/RecordingStats.h"
#include "core/FileSystem.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

namespace lectern::capture {

struct DiskSpacePolicy {
    std::uint64_t warnBytes = 5ull << 30;      ///< < 5 GB free → warning
    std::uint64_t criticalBytes = 1ull << 30;  ///< < 1 GB free → auto-stop
    double warnSeconds = 15 * 60;              ///< < 15 min at current rate → warning
    double criticalSeconds = 2 * 60;           ///< < 2 min at current rate → auto-stop
};

/// Predicts when the output volume fills up (docs/RECORDING_ENGINE.md §9).
class DiskSpaceMonitor {
public:
    using Query = std::function<Result<fs::DiskSpace>(const std::filesystem::path&)>;

    struct Status {
        DiskLevel level = DiskLevel::Ok;
        std::uint64_t availableBytes = 0;
        double bytesPerSecond = 0;
        double secondsRemaining = -1;  ///< < 0 = unknown / effectively infinite
        bool queryFailed = false;
    };

    DiskSpaceMonitor(DiskSpacePolicy policy, Query query = &fs::queryDiskSpace)
        : policy_(policy), query_(std::move(query)) {}

    /// `totalBytesWritten` is the sum over all tracks; `nowNs` any monotonic ns.
    Status update(const std::filesystem::path& path, std::uint64_t totalBytesWritten, std::int64_t nowNs);
    [[nodiscard]] const Status& last() const noexcept { return last_; }

private:
    DiskSpacePolicy policy_;
    Query query_;
    Status last_;
    std::optional<std::uint64_t> lastBytes_;
    std::int64_t lastNs_ = 0;
    double rate_ = 0;
};

}  // namespace lectern::capture
