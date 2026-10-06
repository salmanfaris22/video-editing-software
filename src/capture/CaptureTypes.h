#pragma once

#include "core/Error.h"
#include "core/Time.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lectern::capture {

enum class SourceKind { Display, Window, Application, Camera, Microphone, SystemAudio, Phone, Synthetic };
enum class TrackRole { Screen, Camera, Phone, Microphone, SystemAudio };
enum class MediaType { Video, Audio };

[[nodiscard]] std::string_view toString(SourceKind kind) noexcept;
[[nodiscard]] std::string_view toString(TrackRole role) noexcept;
[[nodiscard]] std::optional<TrackRole> trackRoleFromString(std::string_view text) noexcept;
[[nodiscard]] std::string_view toString(MediaType type) noexcept;
/// Folder under <project>/media for a role ("screen", "camera", "phone", "audio").
[[nodiscard]] std::string_view mediaFolder(TrackRole role) noexcept;
[[nodiscard]] constexpr MediaType mediaTypeOf(TrackRole role) noexcept {
    return (role == TrackRole::Microphone || role == TrackRole::SystemAudio) ? MediaType::Audio : MediaType::Video;
}

// ---------------------------------------------------------------------------
// Device / target descriptions (enumeration results)

struct DisplayInfo {
    std::string id;
    std::string name;
    int widthPx = 0;
    int heightPx = 0;
    double refreshRate = 60.0;
    bool isMain = false;
};

struct WindowInfo {
    std::string id;
    std::string title;
    std::string applicationName;
    std::string applicationId;
    int widthPx = 0;
    int heightPx = 0;
    bool onScreen = true;
};

struct ApplicationInfo {
    std::string id;  ///< bundle id / executable path
    std::string name;
    std::int64_t processId = 0;
};

struct ScreenTargets {
    std::vector<DisplayInfo> displays;
    std::vector<WindowInfo> windows;
    std::vector<ApplicationInfo> applications;
};

struct CameraFormat {
    int width = 0;
    int height = 0;
    double minFps = 0;
    double maxFps = 0;
};

struct CameraInfo {
    std::string id;
    std::string name;
    std::string model;
    bool builtIn = false;
    bool continuity = false;  ///< e.g. an iPhone via Continuity Camera
    std::vector<CameraFormat> formats;
};

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    int channels = 0;
    int sampleRate = 0;
    bool isDefault = false;
    std::string transport;  ///< "built-in", "usb", "bluetooth", "virtual", ...
};

// ---------------------------------------------------------------------------
// Source configuration

enum class ScreenTargetKind { Display, Window, Application };

struct ScreenCaptureTarget {
    ScreenTargetKind kind = ScreenTargetKind::Display;
    std::string id;         ///< display id, window id or application id
    std::string displayId;  ///< for Application targets: the display to capture on
};

struct ScreenCaptureConfig {
    ScreenCaptureTarget target;
    int maxWidth = 1920;   ///< output is scaled to fit (aspect preserved); 0 = native
    int maxHeight = 1080;
    FrameRate frameRate{30, 1};
    bool showCursor = true;
    bool excludeOwnApplication = true;
    int queueDepth = 6;
};

struct CameraCaptureConfig {
    std::string deviceId;  ///< empty = default camera
    int maxWidth = 1920;
    int maxHeight = 1080;
    FrameRate frameRate{30, 1};
};

struct AudioCaptureConfig {
    std::string deviceId;  ///< empty = system default input
    int preferredSampleRate = 48'000;
    bool excludeOwnApplication = true;  ///< system audio: do not record our own sounds
};

// ---------------------------------------------------------------------------
// Permissions

enum class PermissionKind { ScreenCapture, Camera, Microphone, SystemAudio };
enum class PermissionStatus { Granted, Denied, NotDetermined, Restricted, NotApplicable };

[[nodiscard]] std::string_view toString(PermissionKind kind) noexcept;
[[nodiscard]] std::string_view toString(PermissionStatus status) noexcept;

// ---------------------------------------------------------------------------
// Source lifecycle events (delivered from capture threads)

struct SourceEvent {
    /// VideoEffectStarted/Stopped: the OS composites an effect into the
    /// stream (macOS Presenter Overlay draws the camera into the screen).
    enum class Type { Started, Stopped, Interrupted, Resumed, Error, FormatChanged, VideoEffectStarted, VideoEffectStopped };
    Type type = Type::Started;
    std::string message;
};

[[nodiscard]] std::string_view toString(SourceEvent::Type type) noexcept;

}  // namespace lectern::capture
