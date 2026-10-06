
#include "platform/macos/MacInternal.h"  // must be first (see header)

#include "core/Clock.h"
#include "core/Log.h"

extern "C" {
#include <libavutil/hwcontext_videotoolbox.h>
}

namespace lectern::platform::mac {

std::int64_t hostNsFromCMTime(CMTime time) {
    if (!CMTIME_IS_NUMERIC(time)) return HostClock::now();
    // The host time clock counts mach_absolute_time units; convert through
    // them so the result is bit-identical to HostClock::now()'s domain.
    return HostClock::nativeTicksToNs(CMClockConvertHostTimeToSystemUnits(time));
}

AVBufferRef* HwFramesCache::framesContext(int width, int height, AVPixelFormat swFormat) {
    std::lock_guard lock(mutex_);
    if (!device_) {
        AVBufferRef* dev = nullptr;
        if (const int ret = av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0); ret < 0) {
            LEC_ERROR("platform", "VideoToolbox device: {}", media::ffErrorString(ret));
            return nullptr;
        }
        device_ = media::BufferRef::adopt(dev);
    }
    const auto key = std::make_tuple(width, height, static_cast<int>(swFormat));
    if (auto it = frames_.find(key); it != frames_.end()) return it->second.get();

    media::BufferRef frames = media::BufferRef::adopt(av_hwframe_ctx_alloc(device_.get()));
    if (!frames) return nullptr;
    auto* fc = reinterpret_cast<AVHWFramesContext*>(frames.get()->data);
    fc->format = AV_PIX_FMT_VIDEOTOOLBOX;
    fc->sw_format = swFormat;
    fc->width = width;
    fc->height = height;
    if (const int ret = av_hwframe_ctx_init(frames.get()); ret < 0) {
        LEC_ERROR("platform", "VideoToolbox frames context {}x{} {}: {}", width, height,
                  media::pixelFormatName(swFormat), media::ffErrorString(ret));
        return nullptr;
    }
    AVBufferRef* raw = frames.get();
    frames_.emplace(key, std::move(frames));
    return raw;
}

namespace {

void releasePixelBuffer(void* opaque, std::uint8_t*) { CVPixelBufferRelease(static_cast<CVPixelBufferRef>(opaque)); }

template <class T>
T copyAttachment(CVPixelBufferRef pb, CFStringRef key) {
    CFTypeRef v = CVBufferCopyAttachment(pb, key, nullptr);
    return static_cast<T>(v);
}

void applyColor(AVFrame* f, CVPixelBufferRef pb, OSType format) {
    const bool fullRange = format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
                           format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
    f->color_range = fullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    f->colorspace = AVCOL_SPC_BT709;
    f->color_primaries = AVCOL_PRI_BT709;
    f->color_trc = AVCOL_TRC_BT709;

    if (CFStringRef m = copyAttachment<CFStringRef>(pb, kCVImageBufferYCbCrMatrixKey)) {
        if (CFEqual(m, kCVImageBufferYCbCrMatrix_ITU_R_601_4)) f->colorspace = AVCOL_SPC_SMPTE170M;
        else if (CFEqual(m, kCVImageBufferYCbCrMatrix_ITU_R_2020)) f->colorspace = AVCOL_SPC_BT2020_NCL;
        CFRelease(m);
    }
    if (CFStringRef p = copyAttachment<CFStringRef>(pb, kCVImageBufferColorPrimariesKey)) {
        if (CFEqual(p, kCVImageBufferColorPrimaries_P3_D65)) f->color_primaries = AVCOL_PRI_SMPTE432;
        else if (CFEqual(p, kCVImageBufferColorPrimaries_ITU_R_2020)) f->color_primaries = AVCOL_PRI_BT2020;
        else if (CFEqual(p, kCVImageBufferColorPrimaries_SMPTE_C)) f->color_primaries = AVCOL_PRI_SMPTE170M;
        CFRelease(p);
    }
    if (CFStringRef t = copyAttachment<CFStringRef>(pb, kCVImageBufferTransferFunctionKey)) {
        if (CFEqual(t, kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ)) f->color_trc = AVCOL_TRC_SMPTE2084;
        else if (CFEqual(t, kCVImageBufferTransferFunction_ITU_R_2100_HLG)) f->color_trc = AVCOL_TRC_ARIB_STD_B67;
        else if (CFEqual(t, kCVImageBufferTransferFunction_sRGB)) f->color_trc = AVCOL_TRC_IEC61966_2_1;
        CFRelease(t);
    }
}

}  // namespace

media::Frame wrapPixelBuffer(CVPixelBufferRef pb, HwFramesCache& cache) {
    if (!pb) return {};
    const OSType cvFormat = CVPixelBufferGetPixelFormatType(pb);
    const AVPixelFormat sw = av_map_videotoolbox_format_to_pixfmt(cvFormat);
    if (sw == AV_PIX_FMT_NONE) return {};
    const int width = static_cast<int>(CVPixelBufferGetWidth(pb));
    const int height = static_cast<int>(CVPixelBufferGetHeight(pb));
    AVBufferRef* framesCtx = cache.framesContext(width, height, sw);
    if (!framesCtx) return {};

    media::Frame frame = media::Frame::alloc();
    if (!frame) return {};
    CVPixelBufferRetain(pb);
    frame->buf[0] = av_buffer_create(reinterpret_cast<std::uint8_t*>(pb), sizeof(pb), &releasePixelBuffer, pb,
                                     AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        CVPixelBufferRelease(pb);
        return {};
    }
    frame->data[3] = reinterpret_cast<std::uint8_t*>(pb);
    frame->format = AV_PIX_FMT_VIDEOTOOLBOX;
    frame->width = width;
    frame->height = height;
    frame->hw_frames_ctx = av_buffer_ref(framesCtx);
    applyColor(frame.get(), pb, cvFormat);
    return frame;
}

}  // namespace lectern::platform::mac
