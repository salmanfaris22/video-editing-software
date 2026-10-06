// ScreenCaptureKit backend: displays, windows, applications and system audio
// (docs/RECORDING_ENGINE.md §12). Frames are IOSurface-backed NV12
// CVPixelBuffers, wrapped zero-copy as AV_PIX_FMT_VIDEOTOOLBOX AVFrames.

#include "platform/macos/MacInternal.h"  // must be first (see header)

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "capture/EncodingPresets.h"
#include "core/Clock.h"
#include "core/Log.h"

#include <atomic>
#include <string>
#include <unistd.h>
#include <vector>

using namespace lectern;
using namespace lectern::capture;

namespace lectern::platform::mac {

/// Receives SCStream callbacks (implemented by the C++ sources).
class StreamBridge {
public:
    virtual ~StreamBridge() = default;
    virtual void onScreenSample(CMSampleBufferRef sample) = 0;
    virtual void onAudioSample(CMSampleBufferRef sample) = 0;
    virtual void onStreamStopped(NSError* error) = 0;
    /// Presenter Overlay (or another system video effect) started/stopped.
    virtual void onVideoEffect(bool /*active*/) {}
};

}  // namespace lectern::platform::mac

@interface LECStreamHandler : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithBridge:(lectern::platform::mac::StreamBridge*)bridge;
@end

@implementation LECStreamHandler {
    lectern::platform::mac::StreamBridge* _bridge;
}
- (instancetype)initWithBridge:(lectern::platform::mac::StreamBridge*)bridge {
    if ((self = [super init])) _bridge = bridge;
    return self;
}
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
    (void)stream;
    if (type == SCStreamOutputTypeScreen) {
        _bridge->onScreenSample(sampleBuffer);
    } else if (type == SCStreamOutputTypeAudio) {
        _bridge->onAudioSample(sampleBuffer);
    }
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    (void)stream;
    _bridge->onStreamStopped(error);
}
- (void)outputVideoEffectDidStartForStream:(SCStream*)stream API_AVAILABLE(macos(14.0)) {
    (void)stream;
    _bridge->onVideoEffect(true);
}
- (void)outputVideoEffectDidStopForStream:(SCStream*)stream API_AVAILABLE(macos(14.0)) {
    (void)stream;
    _bridge->onVideoEffect(false);
}
@end

namespace lectern::platform::mac {

namespace {

constexpr NSInteger kUserDeclined = -3801;  // SCStreamErrorUserDeclined

Error scError(NSError* error, const std::string& what) {
    if (!error) return Error(ErrorCode::Unknown, what);
    const ErrorCode code = error.code == kUserDeclined ? ErrorCode::PermissionDenied : ErrorCode::DeviceBusy;
    return Error(code, what + ": " + std::string((error.localizedDescription ? error.localizedDescription.UTF8String : "unknown")), error.code);
}

Result<SCShareableContent*> fetchShareableContent() {
    __block SCShareableContent* content = nil;
    __block NSError* error = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent* c, NSError* e) {
                                                   content = c;
                                                   error = e;
                                                   dispatch_semaphore_signal(done);
                                                 }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0) {
        return fail(ErrorCode::Timeout, "ScreenCaptureKit did not answer");
    }
    if (!content) return fail(scError(error, "list shareable content (is Screen Recording permission granted?)"));
    return content;
}

Resolution displayPixelSize(CGDirectDisplayID display) {
    Resolution r{static_cast<int>(CGDisplayPixelsWide(display)), static_cast<int>(CGDisplayPixelsHigh(display))};
    if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display)) {
        r = {static_cast<int>(CGDisplayModeGetPixelWidth(mode)), static_cast<int>(CGDisplayModeGetPixelHeight(mode))};
        CGDisplayModeRelease(mode);
    }
    return r;
}

std::string displayName(CGDirectDisplayID display, int index) {
    if (CGDisplayIsBuiltin(display)) return "Built-in Display";
    return "Display " + std::to_string(index + 1);
}

std::string str(NSString* s) { return s ? std::string((s.UTF8String ? s.UTF8String : "")) : std::string(); }

bool isOwnProcess(SCRunningApplication* app) { return app && app.processID == getpid(); }

bool isCapturableWindow(SCWindow* w) {
    return w.windowLayer == 0 && w.frame.size.width >= 64 && w.frame.size.height >= 64 && w.owningApplication &&
           !isOwnProcess(w.owningApplication) && w.owningApplication.applicationName.length > 0;
}

SCDisplay* findDisplay(SCShareableContent* content, const std::string& id) {
    for (SCDisplay* d in content.displays) {
        if (std::to_string(d.displayID) == id) return d;
    }
    if (id.empty() || id == "main") {
        const CGDirectDisplayID main = CGMainDisplayID();
        for (SCDisplay* d in content.displays) {
            if (d.displayID == main) return d;
        }
        return content.displays.firstObject;
    }
    return nil;
}

SCRunningApplication* ownApplication(SCShareableContent* content) {
    for (SCRunningApplication* a in content.applications) {
        if (isOwnProcess(a)) return a;
    }
    return nil;
}

dispatch_queue_t makeQueue(const char* label) {
    dispatch_queue_attr_t attr =
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
    return dispatch_queue_create(label, attr);
}

Status startStream(SCStream* stream) {
    __block NSError* error = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [stream startCaptureWithCompletionHandler:^(NSError* e) {
      error = e;
      dispatch_semaphore_signal(done);
    }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0) {
        return fail(ErrorCode::Timeout, "screen capture did not start");
    }
    if (error) return fail(scError(error, "start screen capture"));
    return ok();
}

void stopStream(SCStream* stream) {
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [stream stopCaptureWithCompletionHandler:^(NSError*) { dispatch_semaphore_signal(done); }];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
}

// ===========================================================================

class MacScreenSource final : public IVideoSource, public StreamBridge {
public:
    explicit MacScreenSource(ScreenCaptureConfig config) : config_(std::move(config)) {
        info_.kind = config_.target.kind == ScreenTargetKind::Display     ? SourceKind::Display
                     : config_.target.kind == ScreenTargetKind::Window    ? SourceKind::Window
                                                                          : SourceKind::Application;
        info_.deviceId = config_.target.id;
        info_.nominalFrameRate = config_.frameRate;
    }
    ~MacScreenSource() override { stop(); }

    Status start(IVideoFrameSink& sink) override {
        auto content = fetchShareableContent();
        if (!content) return fail(std::move(content).error());
        SCShareableContent* c = *content;

        SCContentFilter* filter = nil;
        Resolution native;
        switch (config_.target.kind) {
            case ScreenTargetKind::Display: {
                SCDisplay* display = findDisplay(c, config_.target.id);
                if (!display) return fail(ErrorCode::NotFound, "display " + config_.target.id + " not found");
                SCRunningApplication* own = config_.excludeOwnApplication ? ownApplication(c) : nil;
                filter = own ? [[SCContentFilter alloc] initWithDisplay:display
                                                  excludingApplications:@[ own ]
                                                       exceptingWindows:@[]]
                             : [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
                native = displayPixelSize(display.displayID);
                info_.name = displayName(display.displayID, 0);
                break;
            }
            case ScreenTargetKind::Window: {
                SCWindow* window = nil;
                for (SCWindow* w in c.windows) {
                    if (std::to_string(w.windowID) == config_.target.id) window = w;
                }
                if (!window) return fail(ErrorCode::NotFound, "window " + config_.target.id + " not found");
                filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
                const double scale = filter.pointPixelScale > 0 ? filter.pointPixelScale : 2.0;
                native = {static_cast<int>(window.frame.size.width * scale),
                          static_cast<int>(window.frame.size.height * scale)};
                info_.name = str(window.owningApplication.applicationName) + " — " + str(window.title);
                break;
            }
            case ScreenTargetKind::Application: {
                SCRunningApplication* app = nil;
                for (SCRunningApplication* a in c.applications) {
                    if (str(a.bundleIdentifier) == config_.target.id || std::to_string(a.processID) == config_.target.id) {
                        app = a;
                    }
                }
                if (!app) return fail(ErrorCode::NotFound, "application " + config_.target.id + " not found");
                SCDisplay* display = findDisplay(c, config_.target.displayId);
                if (!display) return fail(ErrorCode::NotFound, "no display for application capture");
                filter = [[SCContentFilter alloc] initWithDisplay:display includingApplications:@[ app ] exceptingWindows:@[]];
                native = displayPixelSize(display.displayID);
                info_.name = str(app.applicationName);
                break;
            }
        }

        const Resolution out = fitWithin(native, {config_.maxWidth, config_.maxHeight});
        info_.width = out.width;
        info_.height = out.height;

        SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
        cfg.width = static_cast<size_t>(out.width);
        cfg.height = static_cast<size_t>(out.height);
        const Rational fps = config_.frameRate.rational();
        cfg.minimumFrameInterval = CMTimeMake(fps.den(), static_cast<int32_t>(fps.num()));
        cfg.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        cfg.colorSpaceName = kCGColorSpaceSRGB;  // color-match P3 panels into sRGB/BT.709
        cfg.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_709_2;
        cfg.showsCursor = config_.showCursor;
        cfg.queueDepth = std::clamp(config_.queueDepth, 3, 8);
        cfg.scalesToFit = YES;
        cfg.preservesAspectRatio = YES;
        cfg.captureResolution = SCCaptureResolutionBest;
        cfg.capturesAudio = NO;

        sink_ = &sink;
        handler_ = [[LECStreamHandler alloc] initWithBridge:this];
        queue_ = makeQueue("lectern.capture.screen");
        stream_ = [[SCStream alloc] initWithFilter:filter configuration:cfg delegate:handler_];
        NSError* error = nil;
        if (![stream_ addStreamOutput:handler_ type:SCStreamOutputTypeScreen sampleHandlerQueue:queue_ error:&error]) {
            stream_ = nil;
            return fail(scError(error, "add screen output"));
        }
        running_.store(true, std::memory_order_release);
        if (auto st = startStream(stream_); !st) {
            running_.store(false, std::memory_order_release);
            stream_ = nil;
            return st;
        }
        LEC_INFO("platform", "ScreenCaptureKit '{}' {}x{} → {}x{} @ {} fps (queue depth {})", info_.name, native.width,
                 native.height, out.width, out.height, config_.frameRate.toDouble(), cfg.queueDepth);
        sink.onSourceEvent({SourceEvent::Type::Started, info_.name});
        return ok();
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        if (stream_) stopStream(stream_);
        if (queue_) dispatch_sync(queue_, ^{});  // no callbacks after this point
        stream_ = nil;
        handler_ = nil;
        if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, info_.name});
    }

    [[nodiscard]] VideoSourceInfo info() const override { return info_; }

    void onScreenSample(CMSampleBufferRef sample) override {
        if (!running_.load(std::memory_order_acquire) || !CMSampleBufferIsValid(sample)) return;
        CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
        if (!attachments || CFArrayGetCount(attachments) == 0) return;
        auto* dict = (__bridge NSDictionary*)CFArrayGetValueAtIndex(attachments, 0);
        NSNumber* status = dict[SCStreamFrameInfoStatus];
        // Idle/blank/suspended buffers carry no new image; the CFR pacer
        // repeats the last frame instead.
        if (!status || status.integerValue != SCFrameStatusComplete) return;
        CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sample);
        if (!pb) return;
        media::Frame frame = wrapPixelBuffer(pb, frames_);
        if (!frame) return;
        const std::int64_t host = hostNsFromCMTime(CMSampleBufferGetPresentationTimeStamp(sample));
        sink_->onVideoFrame(CapturedVideoFrame{std::move(frame), host, sequence_++});
    }

    void onAudioSample(CMSampleBufferRef) override {}

    void onStreamStopped(NSError* error) override {
        if (!running_.load(std::memory_order_acquire)) return;
        // E.g. the captured window was closed or the display disconnected.
        sink_->onSourceEvent({SourceEvent::Type::Error, scError(error, "screen capture stopped").message()});
    }

    void onVideoEffect(bool active) override {
        if (!running_.load(std::memory_order_acquire)) return;
        // Presenter Overlay draws the camera into this stream, and the camera
        // device then delivers a placeholder: the tracks stop being separate.
        LEC_WARN("platform", "screen capture: system video effect {} (Presenter Overlay)", active ? "started" : "stopped");
        sink_->onSourceEvent({active ? SourceEvent::Type::VideoEffectStarted : SourceEvent::Type::VideoEffectStopped,
                              "Presenter Overlay"});
    }

private:
    ScreenCaptureConfig config_;
    VideoSourceInfo info_;
    IVideoFrameSink* sink_ = nullptr;
    HwFramesCache frames_;
    std::atomic<bool> running_{false};
    std::uint64_t sequence_ = 0;
    SCStream* stream_ = nil;
    LECStreamHandler* handler_ = nil;
    dispatch_queue_t queue_ = nil;
};

// ===========================================================================

/// System audio through ScreenCaptureKit (all apps except ours). A tiny,
/// low-rate video stream is required by the API and ignored.
class MacSystemAudioSource final : public IAudioSource, public StreamBridge {
public:
    explicit MacSystemAudioSource(AudioCaptureConfig config) : config_(std::move(config)) {}
    ~MacSystemAudioSource() override { stop(); }

    Status start(IAudioSink& sink) override {
        auto content = fetchShareableContent();
        if (!content) return fail(std::move(content).error());
        SCDisplay* display = findDisplay(*content, "main");
        if (!display) return fail(ErrorCode::NotFound, "no display for system audio capture");
        SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];

        SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
        cfg.width = 2;
        cfg.height = 2;
        cfg.minimumFrameInterval = CMTimeMake(1, 1);
        cfg.queueDepth = 3;
        cfg.showsCursor = NO;
        cfg.capturesAudio = YES;
        cfg.sampleRate = 48'000;
        cfg.channelCount = 2;
        cfg.excludesCurrentProcessAudio = config_.excludeOwnApplication;

        sink_ = &sink;
        handler_ = [[LECStreamHandler alloc] initWithBridge:this];
        videoQueue_ = makeQueue("lectern.capture.sysaudio.video");
        audioQueue_ = makeQueue("lectern.capture.sysaudio");
        stream_ = [[SCStream alloc] initWithFilter:filter configuration:cfg delegate:handler_];
        NSError* error = nil;
        if (![stream_ addStreamOutput:handler_ type:SCStreamOutputTypeScreen sampleHandlerQueue:videoQueue_ error:&error] ||
            ![stream_ addStreamOutput:handler_ type:SCStreamOutputTypeAudio sampleHandlerQueue:audioQueue_ error:&error]) {
            stream_ = nil;
            return fail(scError(error, "add system audio output"));
        }
        running_.store(true, std::memory_order_release);
        if (auto st = startStream(stream_); !st) {
            running_.store(false, std::memory_order_release);
            stream_ = nil;
            return st;
        }
        sink.onSourceEvent({SourceEvent::Type::Started, "System Audio"});
        return ok();
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        if (stream_) stopStream(stream_);
        if (audioQueue_) dispatch_sync(audioQueue_, ^{});
        if (videoQueue_) dispatch_sync(videoQueue_, ^{});
        stream_ = nil;
        handler_ = nil;
        if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, "System Audio"});
    }

    [[nodiscard]] AudioSourceInfo info() const override {
        return AudioSourceInfo{"System Audio", SourceKind::SystemAudio, "system-audio", 48'000, 2, 0};
    }

    void onScreenSample(CMSampleBufferRef) override {}

    void onAudioSample(CMSampleBufferRef sample) override {
        if (!running_.load(std::memory_order_acquire) || !CMSampleBufferIsValid(sample)) return;
        CMFormatDescriptionRef fd = CMSampleBufferGetFormatDescription(sample);
        const AudioStreamBasicDescription* asbd = fd ? CMAudioFormatDescriptionGetStreamBasicDescription(fd) : nullptr;
        if (!asbd || asbd->mFormatID != kAudioFormatLinearPCM || !(asbd->mFormatFlags & kAudioFormatFlagIsFloat) ||
            asbd->mBitsPerChannel != 32) {
            return;
        }
        const int channels = static_cast<int>(asbd->mChannelsPerFrame);
        const auto frames = static_cast<int>(CMSampleBufferGetNumSamples(sample));
        if (channels <= 0 || frames <= 0) return;

        std::size_t ablSize = 0;
        CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sample, &ablSize, nullptr, 0, nullptr, nullptr, 0,
                                                                nullptr);
        if (abl_.size() < ablSize) abl_.resize(ablSize);
        auto* abl = reinterpret_cast<AudioBufferList*>(abl_.data());
        CMBlockBufferRef block = nullptr;
        if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
                sample, nullptr, abl, ablSize, nullptr, nullptr, kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
                &block) != noErr) {
            return;
        }
        interleaved_.resize(static_cast<std::size_t>(frames * channels));
        if (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) {
            for (int c = 0; c < channels && c < static_cast<int>(abl->mNumberBuffers); ++c) {
                const auto* src = static_cast<const float*>(abl->mBuffers[c].mData);
                for (int i = 0; i < frames; ++i) interleaved_[static_cast<std::size_t>(i * channels + c)] = src[i];
            }
        } else {
            std::memcpy(interleaved_.data(), abl->mBuffers[0].mData,
                        std::min<std::size_t>(abl->mBuffers[0].mDataByteSize, interleaved_.size() * sizeof(float)));
        }
        if (block) CFRelease(block);
        const std::int64_t host = hostNsFromCMTime(CMSampleBufferGetPresentationTimeStamp(sample));
        sink_->onAudio(AudioChunk{interleaved_.data(), frames, channels, static_cast<int>(asbd->mSampleRate), host, 0});
    }

    void onStreamStopped(NSError* error) override {
        if (!running_.load(std::memory_order_acquire)) return;
        sink_->onSourceEvent({SourceEvent::Type::Error, scError(error, "system audio capture stopped").message()});
    }

private:
    AudioCaptureConfig config_;
    IAudioSink* sink_ = nullptr;
    std::atomic<bool> running_{false};
    std::vector<std::uint8_t> abl_;
    std::vector<float> interleaved_;
    SCStream* stream_ = nil;
    LECStreamHandler* handler_ = nil;
    dispatch_queue_t videoQueue_ = nil;
    dispatch_queue_t audioQueue_ = nil;
};

// ===========================================================================

class MacScreenBackend final : public IScreenCaptureBackend {
public:
    [[nodiscard]] std::string name() const override { return "ScreenCaptureKit"; }

    Result<ScreenTargets> enumerateTargets() override {
        @autoreleasepool {
            auto content = fetchShareableContent();
            if (!content) return fail(std::move(content).error());
            ScreenTargets targets;
            const CGDirectDisplayID main = CGMainDisplayID();
            int index = 0;
            for (SCDisplay* d in (*content).displays) {
                const Resolution px = displayPixelSize(d.displayID);
                double refresh = 60.0;
                if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(d.displayID)) {
                    if (CGDisplayModeGetRefreshRate(mode) > 0) refresh = CGDisplayModeGetRefreshRate(mode);
                    CGDisplayModeRelease(mode);
                }
                targets.displays.push_back({std::to_string(d.displayID), displayName(d.displayID, index++), px.width,
                                            px.height, refresh, d.displayID == main});
            }
            std::vector<pid_t> appsWithWindows;
            for (SCWindow* w in (*content).windows) {
                if (!isCapturableWindow(w)) continue;
                WindowInfo wi;
                wi.id = std::to_string(w.windowID);
                wi.title = str(w.title);
                wi.applicationName = str(w.owningApplication.applicationName);
                wi.applicationId = str(w.owningApplication.bundleIdentifier);
                wi.widthPx = static_cast<int>(w.frame.size.width * 2);
                wi.heightPx = static_cast<int>(w.frame.size.height * 2);
                wi.onScreen = w.onScreen;
                if (wi.title.empty()) wi.title = wi.applicationName;
                targets.windows.push_back(std::move(wi));
                appsWithWindows.push_back(w.owningApplication.processID);
            }
            for (SCRunningApplication* a in (*content).applications) {
                if (isOwnProcess(a) || std::find(appsWithWindows.begin(), appsWithWindows.end(), a.processID) ==
                                           appsWithWindows.end()) {
                    continue;
                }
                const std::string bundle = str(a.bundleIdentifier);
                targets.applications.push_back(
                    {bundle.empty() ? std::to_string(a.processID) : bundle, str(a.applicationName), a.processID});
            }
            return targets;
        }
    }

    Result<std::unique_ptr<IVideoSource>> createSource(const ScreenCaptureConfig& config) override {
        return std::unique_ptr<IVideoSource>(std::make_unique<MacScreenSource>(config));
    }
};

}  // namespace

std::unique_ptr<IScreenCaptureBackend> createScreenBackend() { return std::make_unique<MacScreenBackend>(); }

Result<std::unique_ptr<IAudioSource>> createSystemAudioSource(const AudioCaptureConfig& config) {
    return std::unique_ptr<IAudioSource>(std::make_unique<MacSystemAudioSource>(config));
}

}  // namespace lectern::platform::mac
