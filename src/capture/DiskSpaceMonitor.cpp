#include "capture/DiskSpaceMonitor.h"

namespace lectern::capture {

DiskSpaceMonitor::Status DiskSpaceMonitor::update(const std::filesystem::path& path, std::uint64_t totalBytesWritten,
                                                  std::int64_t nowNs) {
    if (lastBytes_ && nowNs > lastNs_) {
        const double dt = static_cast<double>(nowNs - lastNs_) / 1e9;
        const double instant =
            totalBytesWritten >= *lastBytes_ ? static_cast<double>(totalBytesWritten - *lastBytes_) / dt : 0.0;
        // EMA smooths keyframe bursts and encoder rate-control swings.
        rate_ = rate_ == 0 ? instant : rate_ + 0.3 * (instant - rate_);
    }
    lastBytes_ = totalBytesWritten;
    lastNs_ = nowNs;

    Status s;
    s.bytesPerSecond = rate_;
    auto space = query_(path);
    if (!space) {
        s.queryFailed = true;
        s.level = last_.level;  // keep the previous assessment
        last_ = s;
        return s;
    }
    s.availableBytes = space->available;
    s.secondsRemaining = rate_ > 1.0 ? static_cast<double>(space->available) / rate_ : -1.0;

    const bool critical = s.availableBytes < policy_.criticalBytes ||
                          (s.secondsRemaining >= 0 && s.secondsRemaining < policy_.criticalSeconds);
    const bool warning = s.availableBytes < policy_.warnBytes ||
                         (s.secondsRemaining >= 0 && s.secondsRemaining < policy_.warnSeconds);
    s.level = critical ? DiskLevel::Critical : (warning ? DiskLevel::Warning : DiskLevel::Ok);
    last_ = s;
    return s;
}

}  // namespace lectern::capture
