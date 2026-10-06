#pragma once

#include "capture/CaptureInterfaces.h"
#include "capture/LiveSources.h"
#include "capture/RecordingSession.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lectern::services {

/// What the user picked on the recording screen (or on the CLI).
struct RecordingRequest {
    std::optional<capture::ScreenCaptureTarget> screen;
    std::optional<std::string> cameraId;      ///< empty string = default camera
    std::optional<std::string> microphoneId;  ///< empty string = default input
    bool systemAudio = false;
    /// When true, openSources() will start a PhoneCameraSource and include it
    /// in the LiveSourceSet.  The caller must have called listen() on the
    /// source first (or let openSources() do it on its behalf).
    bool phone = false;
    std::string resolution = "1080p";         ///< 720p | 1080p | 1440p | 2160p | native
    int frameRate = 30;                       ///< 24 | 30 | 60
    std::string quality = "high";             ///< standard | high
    std::string encoder = "auto";             ///< auto | hardware | software
    bool showCursor = true;
    bool excludeOwnApplication = true;
};

/// Running sources for one recording screen. Sources outlive sessions so that
/// previews/meters work before recording and devices are warm at T0.
struct LiveSourceSet {
    std::shared_ptr<capture::LiveVideoSource> screen;
    std::shared_ptr<capture::LiveVideoSource> camera;
    /// Phone used as a wireless camera (Phase 1C).  The underlying
    /// PhoneCameraSource stays listening even while no phone is connected so
    /// the user can open the pairing sheet at any time without re-opening
    /// the source.
    std::shared_ptr<capture::LiveVideoSource> phone;
    std::shared_ptr<capture::LiveAudioSource> microphone;
    std::shared_ptr<capture::LiveAudioSource> systemAudio;
    std::vector<std::string> warnings;  ///< sources that could not be opened

    [[nodiscard]] bool empty() const noexcept { return !screen && !camera && !phone && !microphone && !systemAudio; }
    void stopAll();
};

/// Starts the requested sources. A source that fails to open is skipped and
/// reported in `warnings` (the user can still record the others); fails only
/// if nothing could be opened.
[[nodiscard]] Result<LiveSourceSet> openSources(capture::CaptureBackends& backends, const RecordingRequest& request);

/// Opens just the screen source (done at countdown start; see RECORDING_ENGINE.md §2).
[[nodiscard]] Result<std::shared_ptr<capture::LiveVideoSource>> openScreenSource(
    capture::CaptureBackends& backends, const RecordingRequest& request);

/// Session configuration with encoding settings derived from the request and
/// the actual source formats.
[[nodiscard]] capture::SessionConfig makeSessionConfig(const LiveSourceSet& sources, const RecordingRequest& request,
                                                       const std::filesystem::path& projectDir, std::string title);

}  // namespace lectern::services
