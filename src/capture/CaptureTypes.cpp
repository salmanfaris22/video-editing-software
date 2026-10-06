#include "capture/CaptureTypes.h"

namespace lectern::capture {

std::string_view toString(SourceKind kind) noexcept {
    switch (kind) {
        case SourceKind::Display: return "display";
        case SourceKind::Window: return "window";
        case SourceKind::Application: return "application";
        case SourceKind::Camera: return "camera";
        case SourceKind::Microphone: return "microphone";
        case SourceKind::SystemAudio: return "system-audio";
        case SourceKind::Phone: return "phone";
        case SourceKind::Synthetic: return "synthetic";
    }
    return "unknown";
}

std::string_view toString(TrackRole role) noexcept {
    switch (role) {
        case TrackRole::Screen: return "screen";
        case TrackRole::Camera: return "camera";
        case TrackRole::Phone: return "phone";
        case TrackRole::Microphone: return "microphone";
        case TrackRole::SystemAudio: return "systemAudio";
    }
    return "screen";
}

std::optional<TrackRole> trackRoleFromString(std::string_view text) noexcept {
    for (TrackRole r : {TrackRole::Screen, TrackRole::Camera, TrackRole::Phone, TrackRole::Microphone,
                        TrackRole::SystemAudio}) {
        if (toString(r) == text) return r;
    }
    return std::nullopt;
}

std::string_view toString(MediaType type) noexcept { return type == MediaType::Video ? "video" : "audio"; }

std::string_view mediaFolder(TrackRole role) noexcept {
    switch (role) {
        case TrackRole::Screen: return "screen";
        case TrackRole::Camera: return "camera";
        case TrackRole::Phone: return "phone";
        case TrackRole::Microphone:
        case TrackRole::SystemAudio: return "audio";
    }
    return "other";
}

std::string_view toString(PermissionKind kind) noexcept {
    switch (kind) {
        case PermissionKind::ScreenCapture: return "screen";
        case PermissionKind::Camera: return "camera";
        case PermissionKind::Microphone: return "microphone";
        case PermissionKind::SystemAudio: return "system-audio";
    }
    return "unknown";
}

std::string_view toString(PermissionStatus status) noexcept {
    switch (status) {
        case PermissionStatus::Granted: return "granted";
        case PermissionStatus::Denied: return "denied";
        case PermissionStatus::NotDetermined: return "not-determined";
        case PermissionStatus::Restricted: return "restricted";
        case PermissionStatus::NotApplicable: return "not-applicable";
    }
    return "unknown";
}

std::string_view toString(SourceEvent::Type type) noexcept {
    switch (type) {
        case SourceEvent::Type::Started: return "started";
        case SourceEvent::Type::Stopped: return "stopped";
        case SourceEvent::Type::Interrupted: return "interrupted";
        case SourceEvent::Type::Resumed: return "resumed";
        case SourceEvent::Type::Error: return "error";
        case SourceEvent::Type::FormatChanged: return "format-changed";
        case SourceEvent::Type::VideoEffectStarted: return "video-effect-started";
        case SourceEvent::Type::VideoEffectStopped: return "video-effect-stopped";
    }
    return "unknown";
}

}  // namespace lectern::capture
