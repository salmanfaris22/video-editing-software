#pragma once

#include "capture/SessionClock.h"
#include "capture/SessionManifest.h"
#include "core/Error.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>

namespace lectern::capture {

/// Captures pointer and keyboard events during a recording for auto-zoom and overlays.
class InputEventRecorder {
public:
    InputEventRecorder(const SessionClock& clock, std::filesystem::path outputFile, int captureWidth, int captureHeight);
    ~InputEventRecorder();

    InputEventRecorder(const InputEventRecorder&) = delete;
    InputEventRecorder& operator=(const InputEventRecorder&) = delete;

    Status start();
    void stop();
    [[nodiscard]] InputEventLog snapshot() const;
    [[nodiscard]] const SessionClock& sessionClock() const noexcept { return clock_; }
    Status flush();

    /// Called from the platform event tap (host time → session time).
    void ingestPointer(std::int64_t hostNs, std::string_view type, double x, double y, int button);
    void ingestKey(std::int64_t hostNs, std::string_view type, std::string key, std::uint32_t modifiers);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    const SessionClock& clock_;
    std::filesystem::path outputFile_;
    int captureWidth_;
    int captureHeight_;
    mutable std::mutex mutex_;
    InputEventLog log_;
    std::atomic<bool> running_{false};

    void recordPointer(Time sessionTime, std::string_view type, double x, double y, int button);
    void recordKey(Time sessionTime, std::string_view type, std::string key, std::uint32_t modifiers);
};

Status startPlatformCapture(InputEventRecorder* owner);
void stopPlatformCapture(InputEventRecorder* owner);

}  // namespace lectern::capture
