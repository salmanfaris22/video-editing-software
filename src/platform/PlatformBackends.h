#pragma once

#include "capture/CaptureInterfaces.h"

#include <cstdint>
#include <string>

namespace lectern::platform {

struct PlatformInfo {
    std::string os;               ///< "macOS 26.4", "Windows", "Linux"
    bool nativeScreen = false;    ///< false → synthetic fallback
    bool nativeCamera = false;
    bool nativeAudio = false;
    bool nativeSystemAudio = false;
};

/// Capture backends for the running OS (docs/ARCHITECTURE.md §8). Any backend
/// not implemented for this OS is replaced by its synthetic counterpart, so
/// the application always has a working (if simulated) capture path.
[[nodiscard]] capture::CaptureBackends createCaptureBackends();
[[nodiscard]] PlatformInfo platformInfo();

/// Process-level platform setup (e.g. NSApplication policy for CLI tools).
void initializePlatform();

/// Keeps one of our own windows (native handle, e.g. QWindow::winId()) out of
/// screen recordings where the OS supports per-window exclusion (Windows 10
/// 2004+). No-op on macOS, whose capture filter excludes the whole app.
void excludeWindowFromCapture(std::uintptr_t nativeWindow);

}  // namespace lectern::platform
