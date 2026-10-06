// AVFoundation camera backend (built-in, USB/external, Continuity Camera).

#include "platform/macos/MacInternal.h"  // must be first (see header)

#import <AVFoundation/AVFoundation.h>

#include "core/Log.h"

#include <atomic>
#include <cmath>

using namespace lectern;
using namespace lectern::capture;

namespace lectern::platform::mac {
class CameraBridge {
public:
    virtual ~CameraBridge() = default;
    virtual void onCameraSample(CMSampleBufferRef sample) = 0;
    virtual void onCameraDrop() = 0;
};
}  // namespace lectern::platform::mac

@interface LECCameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
- (instancetype)initWithBridge:(lectern::platform::mac::CameraBridge*)bridge;
@end

@implementation LECCameraDelegate {
    lectern::platform::mac::CameraBridge* _bridge;
}
- (instancetype)initWithBridge:(lectern::platform::mac::CameraBridge*)bridge {
    if ((self = [super init])) _bridge = bridge;
    return self;
}
- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
    (void)output;
    (void)connection;
    _bridge->onCameraSample(sampleBuffer);
}
- (void)captureOutput:(AVCaptureOutput*)output
      didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
    (void)output;
    (void)sampleBuffer;
    (void)connection;
    _bridge->onCameraDrop();
}
@end

namespace lectern::platform::mac {

namespace {

std::string str(NSString* s) { return s ? std::string((s.UTF8String ? s.UTF8String : "")) : std::string(); }

NSArray<AVCaptureDeviceType>* cameraTypes() {
    return @[
        AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeExternal, AVCaptureDeviceTypeContinuityCamera,
        AVCaptureDeviceTypeDeskViewCamera
    ];
}

NSArray<AVCaptureDevice*>* cameras() {
    AVCaptureDeviceDiscoverySession* ds =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:cameraTypes()
                                                               mediaType:AVMediaTypeVideo
                                                                position:AVCaptureDevicePositionUnspecified];
    return ds.devices;
}

/// Best format: largest resolution within the bounds whose frame-rate ranges
/// include the requested rate; falls back to the closest available.
AVCaptureDeviceFormat* chooseFormat(AVCaptureDevice* device, int maxW, int maxH, double fps) {
    AVCaptureDeviceFormat* best = nil;
    long long bestScore = -1;
    for (AVCaptureDeviceFormat* f in device.formats) {
        const CMVideoDimensions d = CMVideoFormatDescriptionGetDimensions(f.formatDescription);
        bool fpsOk = false;
        for (AVFrameRateRange* r in f.videoSupportedFrameRateRanges) {
            if (r.minFrameRate <= fps + 0.01 && r.maxFrameRate >= fps - 0.01) fpsOk = true;
        }
        const bool fits = (maxW <= 0 || d.width <= maxW) && (maxH <= 0 || d.height <= maxH);
        // Prefer: fits bounds, supports fps, then pixel count.
        const long long score = (fits ? (1LL << 50) : 0) + (fpsOk ? (1LL << 49) : 0) +
                                (fits ? static_cast<long long>(d.width) * d.height
                                      : -static_cast<long long>(d.width) * d.height);
        if (score > bestScore) {
            bestScore = score;
            best = f;
        }
    }
    return best;
}

class MacCameraSource final : public IVideoSource, public CameraBridge {
public:
    explicit MacCameraSource(CameraCaptureConfig config) : config_(std::move(config)) {}
    ~MacCameraSource() override { stop(); }

    Status start(IVideoFrameSink& sink) override {
        @autoreleasepool {
            if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo] != AVAuthorizationStatusAuthorized) {
                return fail(ErrorCode::PermissionDenied, "camera access has not been granted");
            }
            AVCaptureDevice* device = nil;
            for (AVCaptureDevice* d in cameras()) {
                if (config_.deviceId.empty() || str(d.uniqueID) == config_.deviceId) {
                    device = d;
                    break;
                }
            }
            if (!device) return fail(ErrorCode::NotFound, "camera '" + config_.deviceId + "' not found");

            session_ = [[AVCaptureSession alloc] init];
            [session_ beginConfiguration];
            NSError* error = nil;
            AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
            if (!input || ![session_ canAddInput:input]) {
                [session_ commitConfiguration];
                return fail(ErrorCode::DeviceBusy,
                            "open camera: " + (error ? str(error.localizedDescription) : std::string("cannot add input")));
            }
            [session_ addInput:input];

            output_ = [[AVCaptureVideoDataOutput alloc] init];
            output_.alwaysDiscardsLateVideoFrames = YES;  // never back-pressure the camera
            output_.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)};
            delegate_ = [[LECCameraDelegate alloc] initWithBridge:this];
            dispatch_queue_attr_t attr =
                dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
            queue_ = dispatch_queue_create("lectern.capture.camera", attr);
            [output_ setSampleBufferDelegate:delegate_ queue:queue_];
            if (![session_ canAddOutput:output_]) {
                [session_ commitConfiguration];
                return fail(ErrorCode::DeviceBusy, "cannot add camera output");
            }
            [session_ addOutput:output_];

            const double fps = config_.frameRate.toDouble();
            AVCaptureDeviceFormat* format = chooseFormat(device, config_.maxWidth, config_.maxHeight, fps);
            if (format && [device lockForConfiguration:&error]) {
                device.activeFormat = format;
                for (AVFrameRateRange* r in format.videoSupportedFrameRateRanges) {
                    if (r.minFrameRate <= fps + 0.01 && r.maxFrameRate >= fps - 0.01) {
                        // Cap the rate; the device may still lower it in low light.
                        const Rational q = config_.frameRate.rational();
                        device.activeVideoMinFrameDuration = CMTimeMake(q.den(), static_cast<int32_t>(q.num()));
                        break;
                    }
                }
                [device unlockForConfiguration];
            }
            [session_ commitConfiguration];

            const CMVideoDimensions dims =
                format ? CMVideoFormatDescriptionGetDimensions(format.formatDescription) : CMVideoDimensions{0, 0};
            info_ = VideoSourceInfo{str(device.localizedName), SourceKind::Camera, str(device.uniqueID), dims.width,
                                    dims.height, config_.frameRate};
            sink_ = &sink;
            running_.store(true, std::memory_order_release);

            NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
            auto* source = this;
            disconnectObserver_ = [nc addObserverForName:AVCaptureDeviceWasDisconnectedNotification
                                                  object:device
                                                   queue:nil
                                              usingBlock:^(NSNotification*) {
                                                if (source->running_.load()) {
                                                    source->sink_->onSourceEvent(
                                                        {SourceEvent::Type::Interrupted, "camera disconnected"});
                                                }
                                              }];
            errorObserver_ = [nc addObserverForName:AVCaptureSessionRuntimeErrorNotification
                                             object:session_
                                              queue:nil
                                         usingBlock:^(NSNotification* n) {
                                           NSError* e = n.userInfo[AVCaptureSessionErrorKey];
                                           if (source->running_.load()) {
                                               source->sink_->onSourceEvent(
                                                   {SourceEvent::Type::Error,
                                                    "camera error: " + str(e.localizedDescription)});
                                           }
                                         }];

            [session_ startRunning];  // synchronous; never call on the UI thread
            if (!session_.isRunning) {
                running_.store(false, std::memory_order_release);
                return fail(ErrorCode::DeviceBusy, "camera session failed to start");
            }
            LEC_INFO("platform", "camera '{}' {}x{} @ {} fps", info_.name, info_.width, info_.height, fps);
            sink.onSourceEvent({SourceEvent::Type::Started, info_.name});
            return ok();
        }
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        @autoreleasepool {
            [session_ stopRunning];
            if (queue_) dispatch_sync(queue_, ^{});
            NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
            if (disconnectObserver_) [nc removeObserver:disconnectObserver_];
            if (errorObserver_) [nc removeObserver:errorObserver_];
            disconnectObserver_ = nil;
            errorObserver_ = nil;
            [output_ setSampleBufferDelegate:nil queue:nil];
            session_ = nil;
            output_ = nil;
            delegate_ = nil;
        }
        if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, info_.name});
    }

    [[nodiscard]] VideoSourceInfo info() const override { return info_; }
    [[nodiscard]] std::uint64_t droppedBySource() const override { return dropped_.load(std::memory_order_relaxed); }

    void onCameraSample(CMSampleBufferRef sample) override {
        if (!running_.load(std::memory_order_acquire)) return;
        CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sample);
        if (!pb) return;
        media::Frame frame = wrapPixelBuffer(pb, frames_);
        if (!frame) return;
        // Sample times are on the session's synchronization clock; convert to host time.
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
        CMClockRef sync = session_.synchronizationClock;
        if (sync) pts = CMSyncConvertTime(pts, sync, CMClockGetHostTimeClock());
        sink_->onVideoFrame(CapturedVideoFrame{std::move(frame), hostNsFromCMTime(pts), sequence_++});
    }

    void onCameraDrop() override { dropped_.fetch_add(1, std::memory_order_relaxed); }

private:
    CameraCaptureConfig config_;
    VideoSourceInfo info_;
    IVideoFrameSink* sink_ = nullptr;
    HwFramesCache frames_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::uint64_t sequence_ = 0;
    AVCaptureSession* session_ = nil;
    AVCaptureVideoDataOutput* output_ = nil;
    LECCameraDelegate* delegate_ = nil;
    dispatch_queue_t queue_ = nil;
    id disconnectObserver_ = nil;
    id errorObserver_ = nil;
};

class MacCameraBackend final : public ICameraCaptureBackend {
public:
    [[nodiscard]] std::string name() const override { return "AVFoundation"; }

    Result<std::vector<CameraInfo>> enumerateCameras() override {
        @autoreleasepool {
            std::vector<CameraInfo> out;
            for (AVCaptureDevice* d in cameras()) {
                CameraInfo ci;
                ci.id = str(d.uniqueID);
                ci.name = str(d.localizedName);
                ci.model = str(d.modelID);
                ci.builtIn = [d.deviceType isEqualToString:AVCaptureDeviceTypeBuiltInWideAngleCamera];
                ci.continuity = [d.deviceType isEqualToString:AVCaptureDeviceTypeContinuityCamera];
                for (AVCaptureDeviceFormat* f in d.formats) {
                    const CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(f.formatDescription);
                    double lo = 1e9;
                    double hi = 0;
                    for (AVFrameRateRange* r in f.videoSupportedFrameRateRanges) {
                        lo = std::min(lo, r.minFrameRate);
                        hi = std::max(hi, r.maxFrameRate);
                    }
                    ci.formats.push_back({dim.width, dim.height, lo, hi});
                }
                out.push_back(std::move(ci));
            }
            return out;
        }
    }

    Result<std::unique_ptr<IVideoSource>> createSource(const CameraCaptureConfig& config) override {
        return std::unique_ptr<IVideoSource>(std::make_unique<MacCameraSource>(config));
    }
};

}  // namespace

std::unique_ptr<ICameraCaptureBackend> createCameraBackend() { return std::make_unique<MacCameraBackend>(); }

}  // namespace lectern::platform::mac
