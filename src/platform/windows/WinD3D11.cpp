// Direct3D 11 device creation and BGRA → CPU frame conversion for
// Windows.Graphics.Capture.
//
// Verification status: compile-checked with MinGW-w64; not yet run on Windows.

#include "platform/windows/WinD3D11.h"

#include <d3d10.h>
#include <d3d11_1.h>

#include "capture/EncodingPresets.h"
#include "core/Log.h"
#include "media/VideoFramePool.h"

#include <algorithm>
#include <cstring>

namespace lectern::platform::win {

namespace {

void copyRows(std::uint8_t* dst, int dstStride, const std::uint8_t* src, std::size_t srcStride, std::size_t rowBytes,
              int rows) {
    for (int r = 0; r < rows; ++r) {
        std::memcpy(dst + static_cast<std::ptrdiff_t>(r) * dstStride, src + srcStride * static_cast<std::size_t>(r),
                    rowBytes);
    }
}

class GpuNv12Converter final : public FrameConverter {
public:
    static Result<std::unique_ptr<FrameConverter>> create(ID3D11Device* device, int outW, int outH, FrameRate fps) {
        std::unique_ptr<GpuNv12Converter> c(new GpuNv12Converter());
        c->device_ = device;
        device->GetImmediateContext(&c->context_);
        HRESULT hr = c->device_.As(&c->videoDevice_);
        if (SUCCEEDED(hr)) hr = c->context_.As(&c->videoContext_);
        if (FAILED(hr)) return fail(hrError(hr, "Direct3D video processing unavailable", ErrorCode::Unsupported));
        c->outW_ = outW;
        c->outH_ = outH;
        c->fps_ = fps;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(outW);
        desc.Height = static_cast<UINT>(outH);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_NV12;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = device->CreateTexture2D(&desc, nullptr, &c->output_);
        if (FAILED(hr)) return fail(hrError(hr, "NV12 render target", ErrorCode::Unsupported));
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateTexture2D(&desc, nullptr, &c->staging_);
        if (FAILED(hr)) return fail(hrError(hr, "NV12 staging texture", ErrorCode::Unsupported));

        auto pool = media::VideoFramePool::create(outW, outH, AV_PIX_FMT_NV12);
        if (!pool) return fail(std::move(pool).error());
        c->pool_ = std::move(*pool);
        // Validate the processor now (with a nominal input size) so an
        // unsupported driver falls back before the first frame.
        LEC_TRY(c->ensureProcessor(static_cast<UINT>(outW), static_cast<UINT>(outH)));
        return std::unique_ptr<FrameConverter>(std::move(c));
    }

    Result<media::Frame> convert(ID3D11Texture2D* texture, int contentW, int contentH) override {
        D3D11_TEXTURE2D_DESC src{};
        texture->GetDesc(&src);
        LEC_TRY(ensureProcessor(src.Width, src.Height));
        context_->CopyResource(input_.Get(), texture);  // frees the capture buffer sooner

        const RECT source{0, 0, std::min<LONG>(contentW, static_cast<LONG>(src.Width)),
                          std::min<LONG>(contentH, static_cast<LONG>(src.Height))};
        const capture::PixelRect fit =
            capture::letterbox({static_cast<int>(source.right), static_cast<int>(source.bottom)}, {outW_, outH_});
        const RECT dest{fit.x, fit.y, fit.x + fit.width, fit.y + fit.height};
        videoContext_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &source);
        videoContext_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &dest);

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = inputView_.Get();
        HRESULT hr = videoContext_->VideoProcessorBlt(processor_.Get(), outputView_.Get(), 0, 1, &stream);
        if (FAILED(hr)) return fail(hrError(hr, "video processor blit"));
        context_->CopyResource(staging_.Get(), output_.Get());

        auto frame = pool_->acquire();
        if (!frame) return fail(std::move(frame).error());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return fail(hrError(hr, "map NV12 readback"));
        const auto* base = static_cast<const std::uint8_t*>(mapped.pData);
        AVFrame* f = frame->get();
        // NV12 staging layout: Y rows, then interleaved UV rows at RowPitch × height.
        copyRows(f->data[0], f->linesize[0], base, mapped.RowPitch, static_cast<std::size_t>(outW_), outH_);
        copyRows(f->data[1], f->linesize[1], base + static_cast<std::size_t>(mapped.RowPitch) * static_cast<UINT>(outH_),
                 mapped.RowPitch, static_cast<std::size_t>(outW_), outH_ / 2);
        context_->Unmap(staging_.Get(), 0);
        f->color_range = AVCOL_RANGE_MPEG;
        f->colorspace = AVCOL_SPC_BT709;
        f->color_primaries = AVCOL_PRI_BT709;
        f->color_trc = AVCOL_TRC_IEC61966_2_1;  // sRGB desktop content
        return std::move(*frame);
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "d3d11-video-processor"; }

private:
    GpuNv12Converter() = default;

    /// (Re)creates the processor and our input copy for a capture-texture size.
    Status ensureProcessor(UINT inW, UINT inH) {
        if (processor_ && inW == inW_ && inH == inH_) return ok();
        inputView_.Reset();
        outputView_.Reset();
        processor_.Reset();
        enumerator_.Reset();
        input_.Reset();

        const Rational q = fps_.rational();
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = {static_cast<UINT>(q.num()), static_cast<UINT>(q.den())};
        content.InputWidth = inW;
        content.InputHeight = inH;
        content.OutputFrameRate = content.InputFrameRate;
        content.OutputWidth = static_cast<UINT>(outW_);
        content.OutputHeight = static_cast<UINT>(outH_);
        content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
        HRESULT hr = videoDevice_->CreateVideoProcessorEnumerator(&content, &enumerator_);
        if (FAILED(hr)) return fail(hrError(hr, "video processor enumerator", ErrorCode::Unsupported));
        UINT support = 0;
        if (FAILED(enumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &support)) ||
            !(support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
            return fail(ErrorCode::Unsupported, "video processor cannot read BGRA");
        }
        support = 0;
        if (FAILED(enumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &support)) ||
            !(support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
            return fail(ErrorCode::Unsupported, "video processor cannot write NV12");
        }
        hr = videoDevice_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_);
        if (FAILED(hr)) return fail(hrError(hr, "create video processor", ErrorCode::Unsupported));

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = inW;
        desc.Height = inH;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr = device_->CreateTexture2D(&desc, nullptr, &input_);
        if (FAILED(hr)) return fail(hrError(hr, "converter input texture"));

        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inView{};
        inView.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        hr = videoDevice_->CreateVideoProcessorInputView(input_.Get(), enumerator_.Get(), &inView, &inputView_);
        if (FAILED(hr)) return fail(hrError(hr, "video processor input view", ErrorCode::Unsupported));
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outView{};
        outView.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        hr = videoDevice_->CreateVideoProcessorOutputView(output_.Get(), enumerator_.Get(), &outView, &outputView_);
        if (FAILED(hr)) return fail(hrError(hr, "video processor output view", ErrorCode::Unsupported));

        // Full-range sRGB in, BT.709 limited range out; no driver "enhancements".
        ComPtr<ID3D11VideoContext1> vc1;
        if (SUCCEEDED(videoContext_.As(&vc1))) {
            vc1->VideoProcessorSetStreamColorSpace1(processor_.Get(), 0, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
            vc1->VideoProcessorSetOutputColorSpace1(processor_.Get(), DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
        } else {
            D3D11_VIDEO_PROCESSOR_COLOR_SPACE in{};
            in.RGB_Range = 0;  // 0-255
            in.YCbCr_Matrix = 1;
            D3D11_VIDEO_PROCESSOR_COLOR_SPACE out{};
            out.RGB_Range = 0;
            out.YCbCr_Matrix = 1;  // BT.709
            out.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
            videoContext_->VideoProcessorSetStreamColorSpace(processor_.Get(), 0, &in);
            videoContext_->VideoProcessorSetOutputColorSpace(processor_.Get(), &out);
        }
        videoContext_->VideoProcessorSetStreamFrameFormat(processor_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        videoContext_->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(), 0, FALSE);
        D3D11_VIDEO_COLOR black{};
        black.RGBA.A = 1.0f;
        videoContext_->VideoProcessorSetOutputBackgroundColor(processor_.Get(), FALSE, &black);
        const RECT target{0, 0, outW_, outH_};
        videoContext_->VideoProcessorSetOutputTargetRect(processor_.Get(), TRUE, &target);
        inW_ = inW;
        inH_ = inH;
        LEC_DEBUG("platform", "D3D11 video processor {}x{} → NV12 {}x{}", inW, inH, outW_, outH_);
        return ok();
    }

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<ID3D11Texture2D> input_;
    ComPtr<ID3D11VideoProcessorInputView> inputView_;
    ComPtr<ID3D11Texture2D> output_;
    ComPtr<ID3D11VideoProcessorOutputView> outputView_;
    ComPtr<ID3D11Texture2D> staging_;
    std::unique_ptr<media::VideoFramePool> pool_;
    int outW_ = 0;
    int outH_ = 0;
    UINT inW_ = 0;
    UINT inH_ = 0;
    FrameRate fps_{30, 1};
};

class BgraReadback final : public FrameConverter {
public:
    explicit BgraReadback(ID3D11Device* device) : device_(device) { device->GetImmediateContext(&context_); }

    Result<media::Frame> convert(ID3D11Texture2D* texture, int contentW, int contentH) override {
        D3D11_TEXTURE2D_DESC src{};
        texture->GetDesc(&src);
        if (!staging_ || src.Width != stagingW_ || src.Height != stagingH_) {
            D3D11_TEXTURE2D_DESC desc = src;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            staging_.Reset();
            const HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &staging_);
            if (FAILED(hr)) return fail(hrError(hr, "BGRA staging texture"));
            stagingW_ = src.Width;
            stagingH_ = src.Height;
        }
        const int w = std::min(contentW, static_cast<int>(src.Width)) & ~1;
        const int h = std::min(contentH, static_cast<int>(src.Height)) & ~1;
        if (w < 2 || h < 2) return fail(ErrorCode::InvalidArgument, "empty capture frame");
        if (!pool_ || !pool_->matches(w, h, AV_PIX_FMT_BGRA)) {
            auto pool = media::VideoFramePool::create(w, h, AV_PIX_FMT_BGRA);
            if (!pool) return fail(std::move(pool).error());
            pool_ = std::move(*pool);
        }
        context_->CopyResource(staging_.Get(), texture);
        auto frame = pool_->acquire();
        if (!frame) return fail(std::move(frame).error());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return fail(hrError(hr, "map BGRA readback"));
        copyRows((*frame)->data[0], (*frame)->linesize[0], static_cast<const std::uint8_t*>(mapped.pData),
                 mapped.RowPitch, static_cast<std::size_t>(w) * 4, h);
        context_->Unmap(staging_.Get(), 0);
        (*frame)->color_range = AVCOL_RANGE_JPEG;
        return std::move(*frame);
    }

    [[nodiscard]] std::string_view name() const noexcept override { return "bgra-readback"; }

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> staging_;
    UINT stagingW_ = 0;
    UINT stagingH_ = 0;
    std::unique_ptr<media::VideoFramePool> pool_;
};

}  // namespace

Result<ComPtr<ID3D11Device>> createCaptureDevice() {
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    ComPtr<ID3D11Device> device;
    HRESULT hr = E_FAIL;
    // Prefer the GPU (with video support for the converter), WARP last.
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        for (const UINT flags : {static_cast<UINT>(D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT),
                                 static_cast<UINT>(D3D11_CREATE_DEVICE_BGRA_SUPPORT)}) {
            hr = D3D11CreateDevice(nullptr, type, nullptr, flags, kLevels, static_cast<UINT>(std::size(kLevels)),
                                   D3D11_SDK_VERSION, &device, nullptr, nullptr);
            if (SUCCEEDED(hr)) break;
        }
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) return fail(hrError(hr, "create Direct3D 11 device"));
    ComPtr<ID3D10Multithread> multithread;
    if (SUCCEEDED(device.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
    return device;
}

Result<std::unique_ptr<FrameConverter>> createGpuNv12Converter(ID3D11Device* device, int outWidth, int outHeight,
                                                               FrameRate fps) {
    return GpuNv12Converter::create(device, outWidth, outHeight, fps);
}

std::unique_ptr<FrameConverter> createBgraReadback(ID3D11Device* device) {
    return std::make_unique<BgraReadback>(device);
}

}  // namespace lectern::platform::win
