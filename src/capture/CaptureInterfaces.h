#pragma once

// Platform abstraction for capture (docs/ARCHITECTURE.md §8). Platform
// backends (src/platform/<os>) implement these; the recording engine only
// sees these interfaces.

#include "capture/CaptureTypes.h"
#include "media/Frame.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace lectern::capture {

/// A captured video frame. `frame` is an AVFrame that is either a software
/// frame or a zero-copy hardware frame (e.g. AV_PIX_FMT_VIDEOTOOLBOX wrapping
/// an IOSurface CVPixelBuffer). `hostTimeNs` is the capture time on the host
/// monotonic clock (core/Clock.h).
struct CapturedVideoFrame {
    media::Frame frame;
    std::int64_t hostTimeNs = 0;
    std::uint64_t sequence = 0;

    [[nodiscard]] CapturedVideoFrame ref() const {
        return CapturedVideoFrame{frame.ref(), hostTimeNs, sequence};
    }
};

class IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;
    /// Called on the backend's capture thread. Must return quickly and must
    /// never block on I/O or long locks.
    virtual void onVideoFrame(CapturedVideoFrame&& frame) noexcept = 0;
    virtual void onSourceEvent(const SourceEvent& event) noexcept = 0;
};

/// Audio delivered by a backend: interleaved float32, valid only during the
/// callback. `hostTimeNs` is the latency-compensated capture time of the
/// first frame.
struct AudioChunk {
    static constexpr std::uint32_t kDiscontinuity = 1u << 0;  ///< samples were lost before this chunk

    const float* interleaved = nullptr;
    int frames = 0;
    int channels = 0;
    int sampleRate = 0;
    std::int64_t hostTimeNs = 0;
    std::uint32_t flags = 0;
};

class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    /// May be called on a real-time audio thread: implementations must not
    /// lock, allocate, log or perform I/O.
    virtual void onAudio(const AudioChunk& chunk) noexcept = 0;
    virtual void onSourceEvent(const SourceEvent& event) noexcept = 0;
};

struct VideoSourceInfo {
    std::string name;
    SourceKind kind = SourceKind::Display;
    std::string deviceId;
    int width = 0;   ///< output frame size (0 until known)
    int height = 0;
    FrameRate nominalFrameRate{30, 1};
};

struct AudioSourceInfo {
    std::string name;
    SourceKind kind = SourceKind::Microphone;
    std::string deviceId;
    int sampleRate = 48'000;
    int channels = 1;
    std::int64_t latencyCompensationNs = 0;  ///< already subtracted from chunk timestamps
};

class IVideoSource {
public:
    virtual ~IVideoSource() = default;
    /// Starts delivering frames to `sink`, which must outlive the running state.
    virtual Status start(IVideoFrameSink& sink) = 0;
    /// Stops capture; when it returns no further callbacks will be made.
    virtual void stop() = 0;
    [[nodiscard]] virtual VideoSourceInfo info() const = 0;
    /// Frames the OS/device reported as dropped before reaching us.
    [[nodiscard]] virtual std::uint64_t droppedBySource() const { return 0; }
};

class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual Status start(IAudioSink& sink) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual AudioSourceInfo info() const = 0;
};

/// Displays, windows and applications (spec: ICaptureBackend +
/// IWindowCaptureBackend; one backend because OS APIs enumerate them together).
class IScreenCaptureBackend {
public:
    virtual ~IScreenCaptureBackend() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual Result<ScreenTargets> enumerateTargets() = 0;
    virtual Result<std::unique_ptr<IVideoSource>> createSource(const ScreenCaptureConfig& config) = 0;
};

class ICameraCaptureBackend {
public:
    virtual ~ICameraCaptureBackend() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual Result<std::vector<CameraInfo>> enumerateCameras() = 0;
    virtual Result<std::unique_ptr<IVideoSource>> createSource(const CameraCaptureConfig& config) = 0;
};

class IAudioCaptureBackend {
public:
    virtual ~IAudioCaptureBackend() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual Result<std::vector<AudioDeviceInfo>> enumerateInputs() = 0;
    virtual Result<std::unique_ptr<IAudioSource>> createMicrophoneSource(const AudioCaptureConfig& config) = 0;
    [[nodiscard]] virtual bool supportsSystemAudio() const = 0;
    virtual Result<std::unique_ptr<IAudioSource>> createSystemAudioSource(const AudioCaptureConfig& config) = 0;
};

class IPermissionService {
public:
    virtual ~IPermissionService() = default;
    [[nodiscard]] virtual PermissionStatus status(PermissionKind kind) const = 0;
    /// Asks the OS (may show a system prompt). `done` may run on any thread.
    virtual void request(PermissionKind kind, std::function<void(PermissionStatus)> done) = 0;
};

/// The set of backends for the running platform (or the synthetic set).
struct CaptureBackends {
    std::unique_ptr<IScreenCaptureBackend> screen;
    std::unique_ptr<ICameraCaptureBackend> camera;
    std::unique_ptr<IAudioCaptureBackend> audio;
    std::unique_ptr<IPermissionService> permissions;
};

}  // namespace lectern::capture
