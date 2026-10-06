#pragma once

#include "core/Error.h"
#include "core/Log.h"

#include <filesystem>
#include <string>

namespace lectern {

/// Defaults applied to new recordings (the recording screen starts from these).
struct RecordingDefaults {
    std::string resolution = "1080p";  ///< 720p | 1080p | 1440p | 2160p | native
    int frameRate = 30;                ///< 24 | 30 | 60
    std::string quality = "high";      ///< standard | high
    std::string encoder = "auto";      ///< auto | hardware | software
    bool captureSystemAudio = true;
    bool showCursor = true;
    bool countdown = true;
    int countdownSeconds = 3;
    bool excludeOwnWindows = true;
    std::string lastScreenTarget;      ///< opaque backend id
    std::string lastCameraId;
    std::string lastMicrophoneId;
};

/// User preferences. Strongly typed; persisted as JSON in the app data dir.
struct AppSettings {
    int version = 1;
    std::filesystem::path projectsDirectory;  ///< empty = platform default
    RecordingDefaults recording;
    bool hardwareAcceleration = true;
    LogLevel logLevel = LogLevel::Info;
};

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

    /// Missing file → defaults. Corrupt file → error (caller decides to reset).
    [[nodiscard]] Result<AppSettings> load() const;
    [[nodiscard]] Status save(const AppSettings& settings) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    /// <app data>/settings.json
    [[nodiscard]] static std::filesystem::path defaultPath();

private:
    std::filesystem::path path_;
};

}  // namespace lectern
