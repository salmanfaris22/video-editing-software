#include "services/RecordingSetup.h"

#include "capture/EncodingPresets.h"
#include "core/Log.h"
#include "media/VideoEncoder.h"
#include "mobile/PhoneCameraSource.h"

namespace lectern::services {

using namespace capture;

void LiveSourceSet::stopAll() {
    if (screen) screen->stop();
    if (camera) camera->stop();
    if (phone) phone->stop();
    if (microphone) microphone->stop();
    if (systemAudio) systemAudio->stop();
}

namespace {
Resolution bounds(const RecordingRequest& r) { return resolutionPreset(r.resolution).value_or(Resolution{0, 0}); }
}  // namespace

Result<std::shared_ptr<LiveVideoSource>> openScreenSource(CaptureBackends& backends, const RecordingRequest& request) {
    if (!request.screen) return fail(ErrorCode::InvalidArgument, "no screen target selected");
    ScreenCaptureConfig cfg;
    cfg.target = *request.screen;
    const Resolution b = bounds(request);
    cfg.maxWidth = b.width;
    cfg.maxHeight = b.height;
    cfg.frameRate = FrameRate(request.frameRate, 1);
    cfg.showCursor = request.showCursor;
    cfg.excludeOwnApplication = request.excludeOwnApplication;
    auto source = backends.screen->createSource(cfg);
    if (!source) return fail(std::move(source).error());
    return LiveVideoSource::start(std::move(*source));
}

Result<LiveSourceSet> openSources(CaptureBackends& backends, const RecordingRequest& request) {
    LiveSourceSet set;
    if (request.screen) {
        auto s = openScreenSource(backends, request);
        if (s) {
            set.screen = std::move(*s);
        } else {
            set.warnings.push_back("Screen: " + s.error().message());
        }
    }
    if (request.cameraId) {
        const Resolution b = bounds(request);
        CameraCaptureConfig cfg{*request.cameraId, b.width > 0 ? std::min(b.width, 1920) : 1920,
                                b.height > 0 ? std::min(b.height, 1080) : 1080, FrameRate(request.frameRate, 1)};
        auto src = backends.camera->createSource(cfg);
        auto live = src ? LiveVideoSource::start(std::move(*src)) : Result<std::shared_ptr<LiveVideoSource>>(fail(src.error()));
        if (live) {
            set.camera = std::move(*live);
        } else {
            set.warnings.push_back("Camera: " + live.error().message());
        }
    }
    if (request.phone) {
        // PhoneCameraSource is self-contained: it hosts the HTTP page and
        // accepts frame uploads.  listen() is idempotent if the controller
        // already called it for the pairing sheet.
        auto phoneSrc = std::make_unique<mobile::PhoneCameraSource>();
        if (auto st = phoneSrc->listen(); !st) {
            set.warnings.push_back("Phone camera: " + st.error().message());
        } else {
            auto live = LiveVideoSource::start(std::move(phoneSrc));
            if (live) {
                set.phone = std::move(*live);
            } else {
                set.warnings.push_back("Phone camera: " + live.error().message());
            }
        }
    }
    if (request.microphoneId) {
        AudioCaptureConfig cfg;
        cfg.deviceId = *request.microphoneId;
        auto src = backends.audio->createMicrophoneSource(cfg);
        auto live = src ? LiveAudioSource::start(std::move(*src)) : Result<std::shared_ptr<LiveAudioSource>>(fail(src.error()));
        if (live) {
            set.microphone = std::move(*live);
        } else {
            set.warnings.push_back("Microphone: " + live.error().message());
        }
    }
    if (request.systemAudio && backends.audio->supportsSystemAudio()) {
        AudioCaptureConfig cfg;
        cfg.excludeOwnApplication = request.excludeOwnApplication;
        auto src = backends.audio->createSystemAudioSource(cfg);
        auto live = src ? LiveAudioSource::start(std::move(*src)) : Result<std::shared_ptr<LiveAudioSource>>(fail(src.error()));
        if (live) {
            set.systemAudio = std::move(*live);
        } else {
            set.warnings.push_back("System audio: " + live.error().message());
        }
    }
    for (const auto& w : set.warnings) LEC_WARN("setup", "{}", w);
    if (set.empty()) {
        std::string all;
        for (const auto& w : set.warnings) all += (all.empty() ? "" : "; ") + w;
        return fail(ErrorCode::DeviceBusy, all.empty() ? "no sources selected" : all);
    }
    return set;
}

SessionConfig makeSessionConfig(const LiveSourceSet& sources, const RecordingRequest& request,
                                const std::filesystem::path& projectDir, std::string title) {
    SessionConfig cfg;
    cfg.title = std::move(title);
    cfg.projectDir = projectDir;
    const FrameRate fps(request.frameRate, 1);
    const QualityPreset quality = qualityFromString(request.quality);
    const media::EncoderPreference encoder = media::encoderPreferenceFromString(request.encoder);

    auto videoTrack = [&](const std::shared_ptr<LiveVideoSource>& src, const char* id, TrackRole role) {
        TrackPlan t;
        t.trackId = id;
        t.role = role;
        t.videoSource = src;
        const VideoSourceInfo info = src->info();
        const LiveVideoStats live = src->stats();
        const Resolution size{live.width > 0 ? live.width : info.width, live.height > 0 ? live.height : info.height};
        t.video.frameRate = fps;
        t.video.encoder = encoder;
        t.video.bitRate = videoBitrate(role, size.width > 0 ? size : Resolution{1920, 1080}, fps, quality);
        if (role != TrackRole::Screen) t.video.maxHold = Time::fromSeconds(2);  // static screens repeat forever
        // Phone frames carry their capture time but reach us after Wi-Fi
        // jitter; give them room so a slow frame is placed, not dropped late.
        if (role == TrackRole::Phone) t.video.latencyAllowance = Time::fromMilliseconds(400);
        t.displayName = info.name;
        return t;
    };
    auto audioTrack = [&](const std::shared_ptr<LiveAudioSource>& src, const char* id, TrackRole role) {
        TrackPlan t;
        t.trackId = id;
        t.role = role;
        t.audioSource = src;
        t.displayName = src->info().name;
        return t;
    };
    if (sources.screen) cfg.tracks.push_back(videoTrack(sources.screen, "screen", TrackRole::Screen));
    if (sources.camera) cfg.tracks.push_back(videoTrack(sources.camera, "camera", TrackRole::Camera));
    if (sources.phone) cfg.tracks.push_back(videoTrack(sources.phone, "phone", TrackRole::Phone));
    if (sources.microphone) cfg.tracks.push_back(audioTrack(sources.microphone, "microphone", TrackRole::Microphone));
    if (sources.systemAudio) cfg.tracks.push_back(audioTrack(sources.systemAudio, "system-audio", TrackRole::SystemAudio));
    return cfg;
}

}  // namespace lectern::services
