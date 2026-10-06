#pragma once

// Internal to src/platform/macos (included only by .mm files).
//
// FFmpeg's `enum AVMediaType` collides with AVFoundation's
// `typedef NSString* AVMediaType`. Every Objective-C++ file therefore includes
// this header FIRST: FFmpeg's enum is renamed while its headers are parsed
// (C functions are unaffected, and no engine header uses the type), and
// Apple framework headers are imported afterwards.
#define AVMediaType LecternFFmpegAVMediaType
#include "capture/CaptureInterfaces.h"
#include "media/Frame.h"
#undef AVMediaType

#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>

#include <map>
#include <memory>
#include <mutex>
#include <tuple>

namespace lectern::platform::mac {

/// Host-clock CMTime (ScreenCaptureKit, CMClockGetHostTimeClock) → host ns in
/// the HostClock domain (mach_absolute_time based).
[[nodiscard]] std::int64_t hostNsFromCMTime(CMTime time);

/// Caches the VideoToolbox hw device and one AVHWFramesContext per
/// (size, format), so captured CVPixelBuffers become AV_PIX_FMT_VIDEOTOOLBOX
/// frames without copies and can still be downloaded when a software encoder
/// is used (docs/RECORDING_ENGINE.md §5.1).
class HwFramesCache {
public:
    /// Borrowed pointer, valid for the lifetime of the cache. Null on failure.
    AVBufferRef* framesContext(int width, int height, AVPixelFormat swFormat);

private:
    std::mutex mutex_;
    media::BufferRef device_;
    std::map<std::tuple<int, int, int>, media::BufferRef> frames_;
};

/// Wraps an IOSurface-backed CVPixelBuffer as a hardware AVFrame (retains it,
/// no pixel copy). Color properties come from the buffer's attachments.
[[nodiscard]] media::Frame wrapPixelBuffer(CVPixelBufferRef pixelBuffer, HwFramesCache& cache);

std::unique_ptr<capture::IScreenCaptureBackend> createScreenBackend();
std::unique_ptr<capture::ICameraCaptureBackend> createCameraBackend();
std::unique_ptr<capture::IAudioCaptureBackend> createAudioBackend();
std::unique_ptr<capture::IPermissionService> createPermissionService();

/// ScreenCaptureKit system-audio source (macOS 13+).
Result<std::unique_ptr<capture::IAudioSource>> createSystemAudioSource(const capture::AudioCaptureConfig& config);

}  // namespace lectern::platform::mac
