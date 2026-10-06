// Media Foundation camera backend (built-in, USB/UVC and virtual cameras).
//
// The Source Reader runs in asynchronous mode: each OnReadSample callback
// copies one NV12 frame into a pooled buffer and requests the next, so
// stopping never waits on a device that has stalled. MJPG/YUY2 formats are
// converted (and scaled when needed) by the reader's advanced video
// processing, using hardware MFTs where available.
//
// Verification status: compile-checked with MinGW-w64; not yet run on Windows.

#include "platform/windows/WinCommon.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "capture/ArrivalAnchoredClock.h"
#include "capture/EncodingPresets.h"
#include "core/Clock.h"
#include "core/Log.h"
#include "media/VideoFramePool.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <tuple>
#include <vector>

namespace lectern::platform::win {

using namespace capture;

namespace {

// MFSampleExtension_DeviceReferenceSystemTime (mfcaptureengine.h, Windows 10
// 1803+): QPC time in 100 ns units at which the device captured the sample.
// Declared locally to avoid the capture-engine header and its GUID library.
constexpr GUID kDeviceReferenceSystemTime = {
    0x6523775a, 0xba2d, 0x405f, {0xb2, 0xc5, 0x01, 0xff, 0x88, 0xe2, 0xe8, 0xf6}};

Error cameraError(HRESULT hr, std::string_view what) {
    if (hr == E_ACCESSDENIED) {
        return Error(ErrorCode::PermissionDenied,
                     "camera access is off in Windows Settings > Privacy & security > Camera", static_cast<std::int64_t>(hr));
    }
    return hrError(hr, what);
}

std::string allocatedString(IMFAttributes* attributes, REFGUID key) {
    LPWSTR value = nullptr;
    UINT32 length = 0;
    if (FAILED(attributes->GetAllocatedString(key, &value, &length)) || !value) return {};
    std::string out = toUtf8(value);
    CoTaskMemFree(value);
    return out;
}

/// Owns the array returned by MFEnumDeviceSources.
class DeviceList {
public:
    DeviceList() = default;
    ~DeviceList() {
        for (UINT32 i = 0; i < count_; ++i) items_[i]->Release();
        CoTaskMemFree(items_);
    }
    DeviceList(const DeviceList&) = delete;
    DeviceList& operator=(const DeviceList&) = delete;

    Status enumerate() {
        ComPtr<IMFAttributes> attrs;
        HRESULT hr = MFCreateAttributes(&attrs, 1);
        if (SUCCEEDED(hr)) hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attrs.Get(), &items_, &count_);
        if (FAILED(hr)) return fail(cameraError(hr, "enumerate cameras"));
        return ok();
    }
    [[nodiscard]] UINT32 size() const noexcept { return count_; }
    [[nodiscard]] IMFActivate* operator[](UINT32 i) const noexcept { return items_[i]; }

private:
    IMFActivate** items_ = nullptr;
    UINT32 count_ = 0;
};

struct NativeFormat {
    ComPtr<IMFMediaType> type;
    GUID subtype{};
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 fpsNum = 0;
    UINT32 fpsDen = 1;
    [[nodiscard]] double fps() const noexcept { return fpsDen ? static_cast<double>(fpsNum) / fpsDen : 0.0; }
};

/// Formats the reader can turn into NV12 cheaply; anything else (H.264
/// camera streams, IR/depth formats) is only used when nothing else exists.
int subtypeRank(const GUID& s) {
    if (IsEqualGUID(s, MFVideoFormat_NV12)) return 4;
    if (IsEqualGUID(s, MFVideoFormat_YUY2)) return 3;
    if (IsEqualGUID(s, MFVideoFormat_MJPG)) return 2;
    if (IsEqualGUID(s, MFVideoFormat_RGB32) || IsEqualGUID(s, MFVideoFormat_I420) ||
        IsEqualGUID(s, MFVideoFormat_IYUV) || IsEqualGUID(s, MFVideoFormat_UYVY)) {
        return 1;
    }
    return 0;
}

Result<ComPtr<IMFMediaTypeHandler>> streamTypeHandler(IMFMediaSource* source) {
    ComPtr<IMFPresentationDescriptor> pd;
    HRESULT hr = source->CreatePresentationDescriptor(&pd);
    if (FAILED(hr)) return fail(cameraError(hr, "camera presentation descriptor"));
    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    hr = pd->GetStreamDescriptorByIndex(0, &selected, &sd);
    if (FAILED(hr)) return fail(cameraError(hr, "camera stream descriptor"));
    ComPtr<IMFMediaTypeHandler> handler;
    hr = sd->GetMediaTypeHandler(&handler);
    if (FAILED(hr)) return fail(cameraError(hr, "camera media type handler"));
    return handler;
}

std::vector<NativeFormat> nativeFormats(IMFMediaTypeHandler* handler) {
    std::vector<NativeFormat> out;
    DWORD count = 0;
    if (FAILED(handler->GetMediaTypeCount(&count))) return out;
    for (DWORD i = 0; i < count; ++i) {
        NativeFormat f;
        if (FAILED(handler->GetMediaTypeByIndex(i, &f.type))) continue;
        GUID major{};
        if (FAILED(f.type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || !IsEqualGUID(major, MFMediaType_Video)) continue;
        if (FAILED(f.type->GetGUID(MF_MT_SUBTYPE, &f.subtype))) continue;
        if (FAILED(MFGetAttributeSize(f.type.Get(), MF_MT_FRAME_SIZE, &f.width, &f.height)) || !f.width || !f.height) {
            continue;
        }
        if (FAILED(MFGetAttributeRatio(f.type.Get(), MF_MT_FRAME_RATE, &f.fpsNum, &f.fpsDen)) || !f.fpsDen) {
            f.fpsNum = 0;
            f.fpsDen = 1;
        }
        out.push_back(std::move(f));
    }
    return out;
}

/// Largest format within the bounds that reaches the requested rate, then
/// the rate closest to the request (no 60 fps USB bandwidth for a 30 fps
/// recording), then the cheapest conversion.
const NativeFormat* chooseFormat(const std::vector<NativeFormat>& formats, const CameraCaptureConfig& cfg) {
    const double fps = cfg.frameRate.toDouble();
    auto key = [&](const NativeFormat& f) {
        const bool fits = (cfg.maxWidth <= 0 || static_cast<int>(f.width) <= cfg.maxWidth) &&
                          (cfg.maxHeight <= 0 || static_cast<int>(f.height) <= cfg.maxHeight);
        const long long pixels = static_cast<long long>(f.width) * f.height;
        return std::make_tuple(subtypeRank(f.subtype) > 0, fits, f.fps() + 0.5 >= fps, fits ? pixels : -pixels,
                               -std::abs(f.fps() - fps), subtypeRank(f.subtype));
    };
    const NativeFormat* best = nullptr;
    for (const NativeFormat& f : formats) {
        if (!best || key(*best) < key(f)) best = &f;
    }
    return best;
}

std::vector<CameraFormat> summarizeFormats(const std::vector<NativeFormat>& formats) {
    std::vector<CameraFormat> out;
    for (const NativeFormat& f : formats) {
        auto it = std::find_if(out.begin(), out.end(), [&](const CameraFormat& c) {
            return c.width == static_cast<int>(f.width) && c.height == static_cast<int>(f.height);
        });
        if (it == out.end()) {
            out.push_back({static_cast<int>(f.width), static_cast<int>(f.height), f.fps(), f.fps()});
        } else {
            it->minFps = std::min(it->minFps, f.fps());
            it->maxFps = std::max(it->maxFps, f.fps());
        }
    }
    return out;
}

class MfCameraSource;

/// IMFSourceReaderCallback that forwards to its source until detached.
LECTERN_COM_IMPL_BEGIN
class ReaderCallback final : public IMFSourceReaderCallback {
public:
    explicit ReaderCallback(MfCameraSource* owner)
        : owner_(owner), flushed_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IMFSourceReaderCallback))) {
            *out = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (left == 0) delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT status, DWORD stream, DWORD flags, LONGLONG timestamp,
                                           IMFSample* sample) override;
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override {
        SetEvent(flushed_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }

    /// After this returns no callback touches the owner (waits for one in progress).
    void detach() {
        std::lock_guard lock(mutex_);
        owner_ = nullptr;
    }
    [[nodiscard]] HANDLE flushedEvent() const noexcept { return flushed_; }

private:
    virtual ~ReaderCallback() {
        if (flushed_) CloseHandle(flushed_);
    }

    std::recursive_mutex mutex_;  // recursive: tolerates a synchronous callback from ReadSample
    MfCameraSource* owner_;
    HANDLE flushed_;
    std::atomic<ULONG> refs_{1};
};
LECTERN_COM_IMPL_END

class MfCameraSource final : public IVideoSource {
public:
    explicit MfCameraSource(CameraCaptureConfig config) : config_(std::move(config)) {}
    ~MfCameraSource() override { stop(); }

    Status start(IVideoFrameSink& sink) override {
        ComApartment com;
        if (auto st = open(); !st) {
            teardown();
            return st;
        }
        sink_ = &sink;
        running_.store(true, std::memory_order_release);
        const HRESULT hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) {
            running_.store(false, std::memory_order_release);
            teardown();
            return fail(cameraError(hr, "start camera"));
        }
        sink.onSourceEvent({SourceEvent::Type::Started, info_.name});
        return ok();
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        teardown();
        if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, info_.name});
    }

    [[nodiscard]] VideoSourceInfo info() const override { return info_; }
    [[nodiscard]] std::uint64_t droppedBySource() const override { return dropped_.load(std::memory_order_relaxed); }

    /// Media Foundation work-queue thread, serialized by the callback's lock.
    void onReadSample(HRESULT status, DWORD flags, LONGLONG sampleTime, IMFSample* sample) {
        const std::int64_t arrival = HostClock::now();
        if (!running_.load(std::memory_order_acquire)) return;
        if (FAILED(status) || (flags & MF_SOURCE_READERF_ERROR)) {
            sink_->onSourceEvent({SourceEvent::Type::Interrupted,
                                  hrError(status, "camera stopped (disconnected or taken by another app)").message()});
            return;  // no further reads
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            sink_->onSourceEvent({SourceEvent::Type::Interrupted, "camera stream ended"});
            return;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            if (readOutputFormat() && !pool_->matches(layout_.width, layout_.height, AV_PIX_FMT_NV12)) {
                if (auto pool = media::VideoFramePool::create(layout_.width, layout_.height, AV_PIX_FMT_NV12)) {
                    pool_ = std::move(*pool);
                    info_.width = layout_.width;
                    info_.height = layout_.height;
                    sink_->onSourceEvent({SourceEvent::Type::FormatChanged,
                                          std::to_string(layout_.width) + "x" + std::to_string(layout_.height)});
                }
            }
        }
        if (sample) deliver(sample, sampleTime, arrival);
        const HRESULT hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr)) sink_->onSourceEvent({SourceEvent::Type::Error, hrError(hr, "camera read").message()});
    }

private:
    /// Plane geometry of the reader's NV12 output.
    struct Layout {
        int width = 0;           ///< visible (even) size delivered downstream
        int height = 0;
        UINT32 offsetX = 0;      ///< visible area inside the decoded buffer
        UINT32 offsetY = 0;
        UINT32 allocHeight = 0;  ///< rows per plane in the buffer (may exceed the visible height)
        LONG stride = 0;         ///< default stride for buffers without IMF2DBuffer
        AVColorSpace matrix = AVCOL_SPC_BT709;
    };

    Status open() {
        DeviceList devices;
        LEC_TRY(devices.enumerate());
        IMFActivate* chosen = nullptr;
        for (UINT32 i = 0; i < devices.size(); ++i) {
            const std::string link = allocatedString(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            if (config_.deviceId.empty() || link == config_.deviceId) {
                chosen = devices[i];
                info_.deviceId = link;
                info_.name = allocatedString(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
                break;
            }
        }
        if (!chosen) return fail(ErrorCode::NotFound, "camera '" + config_.deviceId + "' not found");
        info_.kind = SourceKind::Camera;
        info_.nominalFrameRate = config_.frameRate;

        HRESULT hr = chosen->ActivateObject(IID_PPV_ARGS(&source_));
        if (FAILED(hr)) return fail(cameraError(hr, "open camera '" + info_.name + "'"));

        auto handler = streamTypeHandler(source_.Get());
        if (!handler) return fail(std::move(handler).error());
        const std::vector<NativeFormat> formats = nativeFormats(handler->Get());
        const NativeFormat* native = chooseFormat(formats, config_);
        if (!native) return fail(ErrorCode::Unsupported, "camera '" + info_.name + "' reports no video formats");
        hr = (*handler)->SetCurrentMediaType(native->type.Get());
        if (FAILED(hr)) return fail(cameraError(hr, "select camera format"));

        callback_.Attach(new ReaderCallback(this));
        ComPtr<IMFAttributes> attrs;
        hr = MFCreateAttributes(&attrs, 3);
        if (SUCCEEDED(hr)) hr = attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback_.Get());
        if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source_.Get(), attrs.Get(), &reader_);
        if (FAILED(hr)) return fail(cameraError(hr, "create camera reader"));

        // NV12 at the native size, scaled down by the reader only when the
        // camera has no format within the bounds.
        const Resolution out = fitWithin({static_cast<int>(native->width), static_cast<int>(native->height)},
                                         {config_.maxWidth, config_.maxHeight});
        ComPtr<IMFMediaType> type;
        hr = MFCreateMediaType(&type);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(hr)) {
            hr = MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(out.width),
                                    static_cast<UINT32>(out.height));
        }
        if (SUCCEEDED(hr) && native->fpsNum) hr = MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, native->fpsNum, native->fpsDen);
        if (SUCCEEDED(hr)) hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get());
        if (FAILED(hr)) return fail(cameraError(hr, "camera NV12 output"));
        if (!readOutputFormat()) return fail(ErrorCode::Unsupported, "camera output format is unusable");

        auto pool = media::VideoFramePool::create(layout_.width, layout_.height, AV_PIX_FMT_NV12);
        if (!pool) return fail(std::move(pool).error());
        pool_ = std::move(*pool);
        info_.width = layout_.width;
        info_.height = layout_.height;
        LEC_INFO("platform", "camera '{}' {}x{} @ {:.2f} fps (rank {}) → NV12 {}x{}", info_.name, native->width,
                 native->height, native->fps(), subtypeRank(native->subtype), layout_.width, layout_.height);
        return ok();
    }

    bool readOutputFormat() {
        ComPtr<IMFMediaType> current;
        if (FAILED(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current))) return false;
        UINT32 w = 0;
        UINT32 h = 0;
        if (FAILED(MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h)) || w < 2 || h < 2) return false;
        Layout l;
        l.allocHeight = h;
        UINT32 visibleW = w;
        UINT32 visibleH = h;
        MFVideoArea area{};
        if (SUCCEEDED(current->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof area,
                                       nullptr)) &&
            area.Area.cx > 0 && area.Area.cy > 0 && area.OffsetX.value >= 0 && area.OffsetY.value >= 0) {
            l.offsetX = static_cast<UINT32>(area.OffsetX.value);
            l.offsetY = static_cast<UINT32>(area.OffsetY.value);
            visibleW = std::min<UINT32>(static_cast<UINT32>(area.Area.cx), w - l.offsetX);
            visibleH = std::min<UINT32>(static_cast<UINT32>(area.Area.cy), h - l.offsetY);
        }
        l.width = static_cast<int>(visibleW & ~1u);
        l.height = static_cast<int>(visibleH & ~1u);
        UINT32 stride = 0;
        l.stride = SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? static_cast<LONG>(stride)
                                                                                  : static_cast<LONG>(w);
        UINT32 matrix = 0;
        if (SUCCEEDED(current->GetUINT32(MF_MT_YUV_MATRIX, &matrix))) {
            l.matrix = matrix == MFVideoTransferMatrix_BT601 ? AVCOL_SPC_SMPTE170M : AVCOL_SPC_BT709;
        } else {
            l.matrix = h >= 720 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        }
        if (l.width < 2 || l.height < 2 || l.offsetY % 2) return false;
        layout_ = l;
        return true;
    }

    /// Copies the visible NV12 area of a locked buffer into `dst`.
    [[nodiscard]] bool copyNv12(const BYTE* src, LONG pitch, std::size_t available, AVFrame* dst) const {
        if (!src || pitch <= 0) return false;
        const auto p = static_cast<std::size_t>(pitch);
        const std::size_t uvOffset = p * layout_.allocHeight;
        const std::size_t needed = uvOffset + p * (layout_.allocHeight / 2);
        if (available < needed || layout_.offsetX + static_cast<std::size_t>(layout_.width) > p) return false;
        const BYTE* y = src + p * layout_.offsetY + layout_.offsetX;
        const BYTE* uv = src + uvOffset + p * (layout_.offsetY / 2) + (layout_.offsetX & ~1u);
        const auto rowBytes = static_cast<std::size_t>(layout_.width);
        for (int row = 0; row < layout_.height; ++row) {
            std::memcpy(dst->data[0] + static_cast<std::ptrdiff_t>(row) * dst->linesize[0], y + p * row, rowBytes);
        }
        for (int row = 0; row < layout_.height / 2; ++row) {
            std::memcpy(dst->data[1] + static_cast<std::ptrdiff_t>(row) * dst->linesize[1], uv + p * row, rowBytes);
        }
        return true;
    }

    void deliver(IMFSample* sample, LONGLONG sampleTime, std::int64_t arrival) {
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        auto frame = pool_->acquire();
        if (!frame) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        bool copied = false;
        ComPtr<IMF2DBuffer> buffer2d;
        BYTE* data = nullptr;
        LONG pitch = 0;
        if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&data, &pitch))) {
            DWORD length = 0;
            if (FAILED(buffer2d->GetContiguousLength(&length))) length = 0;
            copied = copyNv12(data, pitch, length, frame->get());
            buffer2d->Unlock2D();
        } else {
            DWORD length = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                copied = copyNv12(data, layout_.stride, length, frame->get());
                buffer->Unlock();
            }
        }
        if (!copied) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        AVFrame* f = frame->get();
        f->color_range = AVCOL_RANGE_MPEG;
        f->colorspace = layout_.matrix;
        f->color_primaries = layout_.matrix == AVCOL_SPC_BT709 ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
        f->color_trc = AVCOL_TRC_BT709;
        sink_->onVideoFrame(CapturedVideoFrame{std::move(*frame), captureTime(sample, sampleTime, arrival), sequence_++});
    }

    /// Best available capture time on the host clock: the device's QPC
    /// reference, else a QPC-based sample time, else the sample time
    /// anchored to arrival. Each fallback is permanent for the stream so the
    /// timeline never flips between domains.
    std::int64_t captureTime(IMFSample* sample, LONGLONG sampleTime, std::int64_t arrival) {
        if (mode_ == TimeMode::DeviceReference) {
            UINT64 qpc = 0;
            if (SUCCEEDED(sample->GetUINT64(kDeviceReferenceSystemTime, &qpc)) &&
                plausibleCaptureTime(qpc100nsToNs(qpc), arrival)) {
                return qpc100nsToNs(qpc);
            }
            mode_ = TimeMode::SampleTime;
        }
        const std::int64_t mediaNs = static_cast<std::int64_t>(sampleTime) * 100;
        if (mode_ == TimeMode::SampleTime) {
            if (plausibleCaptureTime(mediaNs, arrival)) return mediaNs;
            mode_ = TimeMode::Anchored;
            LEC_INFO("platform", "camera '{}': timestamps anchored to arrival time", info_.name);
        }
        return anchor_.map(mediaNs, arrival);
    }

    void teardown() {
        if (callback_) callback_->detach();
        if (reader_ && callback_) {
            // Asynchronous readers must be flushed before release.
            ResetEvent(callback_->flushedEvent());
            if (SUCCEEDED(reader_->Flush(MF_SOURCE_READER_ALL_STREAMS))) {
                WaitForSingleObject(callback_->flushedEvent(), 2000);
            }
        }
        if (source_) source_->Shutdown();
        reader_.Reset();
        source_.Reset();
        callback_.Reset();
    }

    enum class TimeMode { DeviceReference, SampleTime, Anchored };

    CameraCaptureConfig config_;
    VideoSourceInfo info_;
    IVideoFrameSink* sink_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::uint64_t sequence_ = 0;
    ComPtr<IMFMediaSource> source_;
    ComPtr<IMFSourceReader> reader_;
    ComPtr<ReaderCallback> callback_;
    std::unique_ptr<media::VideoFramePool> pool_;
    Layout layout_;
    TimeMode mode_ = TimeMode::DeviceReference;
    ArrivalAnchoredClock anchor_;
};

HRESULT STDMETHODCALLTYPE ReaderCallback::OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG timestamp,
                                                       IMFSample* sample) {
    std::lock_guard lock(mutex_);
    if (owner_) owner_->onReadSample(status, flags, timestamp, sample);
    return S_OK;
}

class MfCameraBackend final : public ICameraCaptureBackend {
public:
    [[nodiscard]] std::string name() const override { return "Media Foundation"; }

    Result<std::vector<CameraInfo>> enumerateCameras() override {
        ComApartment com;
        DeviceList devices;
        LEC_TRY(devices.enumerate());
        std::vector<CameraInfo> out;
        for (UINT32 i = 0; i < devices.size(); ++i) {
            CameraInfo ci;
            ci.id = allocatedString(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            ci.name = allocatedString(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            // Opening the device to list its formats does not start streaming.
            ComPtr<IMFMediaSource> source;
            if (SUCCEEDED(devices[i]->ActivateObject(IID_PPV_ARGS(&source)))) {
                if (auto handler = streamTypeHandler(source.Get())) ci.formats = summarizeFormats(nativeFormats(handler->Get()));
                devices[i]->ShutdownObject();
            }
            out.push_back(std::move(ci));
        }
        return out;
    }

    Result<std::unique_ptr<IVideoSource>> createSource(const CameraCaptureConfig& config) override {
        return std::unique_ptr<IVideoSource>(std::make_unique<MfCameraSource>(config));
    }
};

}  // namespace

std::unique_ptr<ICameraCaptureBackend> createCameraBackend() { return std::make_unique<MfCameraBackend>(); }

}  // namespace lectern::platform::win
