#include "editor/Exporter.h"

#include "core/Clock.h"
#include "core/Log.h"
#include "editor/AudioMixer.h"
#include "editor/FrameRenderer.h"
#include "editor/FrameProvider.h"
#include "media/AudioEncoder.h"
#include "media/Muxer.h"
#include "media/VideoEncoder.h"
#include "media/VideoFramePool.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <future>
#include <system_error>

namespace lectern::editor {

namespace {

/// RGB32 canvas → NV12 with BT.709 limited range (what the encoder declares).
class RgbToNv12 {
public:
    Result<media::Frame> convert(const QImage& image, media::VideoFramePool& pool) {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        constexpr AVPixelFormat kRgb32 = AV_PIX_FMT_BGRA;
#else
        constexpr AVPixelFormat kRgb32 = AV_PIX_FMT_ARGB;
#endif
        sws_.reset(sws_getCachedContext(sws_.release(), image.width(), image.height(), kRgb32, pool.width(),
                                        pool.height(), AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!sws_) return fail(ErrorCode::EncoderError, "cannot create color converter");
        sws_setColorspaceDetails(sws_.get(), sws_getCoefficients(SWS_CS_DEFAULT), 1, sws_getCoefficients(SWS_CS_ITU709),
                                 0, 0, 1 << 16, 1 << 16);
        auto frame = pool.acquire();
        if (!frame) return fail(std::move(frame).error());
        const std::uint8_t* src[4] = {image.constBits(), nullptr, nullptr, nullptr};
        const int srcStride[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
        sws_scale(sws_.get(), src, srcStride, 0, image.height(), (*frame)->data, (*frame)->linesize);
        (*frame)->color_range = AVCOL_RANGE_MPEG;
        (*frame)->colorspace = AVCOL_SPC_BT709;
        (*frame)->color_primaries = AVCOL_PRI_BT709;
        (*frame)->color_trc = AVCOL_TRC_BT709;
        return std::move(*frame);
    }

private:
    media::SwsContextPtr sws_;
};

}  // namespace

std::int64_t exportBitrate(int width, int height, FrameRate fps, const std::string& quality, double bitrateScale) {
    double bitsPerPixel = 0.12;
    if (quality == "draft") bitsPerPixel = 0.04;
    else if (quality == "standard") bitsPerPixel = 0.07;
    else if (quality == "max") bitsPerPixel = 0.20;
    else if (quality == "ultra") bitsPerPixel = 0.28;
    bitsPerPixel *= std::clamp(bitrateScale, 0.25, 2.0);
    const double rate = std::clamp(fps.toDouble(), 1.0, 120.0);
    // Higher frame rates need proportionally fewer bits per frame.
    const double effectiveRate = rate <= 30 ? rate : 30 + (rate - 30) * 0.6;
    return std::max<std::int64_t>(500'000, static_cast<std::int64_t>(width * static_cast<double>(height) * effectiveRate *
                                                                      bitsPerPixel));
}

media::MuxerOptions muxerOptionsForExport(const std::filesystem::path& output, const std::string& containerHint) {
    std::string ext = output.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string kind = containerHint;
    if (kind.empty()) {
        if (ext == ".mov") kind = "mov";
        else if (ext == ".mkv") kind = "mkv";
        else kind = "mp4";
    }
    media::MuxerOptions mopts;
    if (kind == "mov") {
        mopts.format = "mov";
        mopts.formatOptions = {{"movflags", "+faststart"}};
    } else if (kind == "mkv") {
        mopts.format = "matroska";
    } else {
        mopts.format = "mp4";
        mopts.formatOptions = {{"movflags", "+faststart"}};
    }
    return mopts;
}

Result<ExportResult> exportProject(const project::Project& project, const std::filesystem::path& projectDir,
                                   const ExportOptions& options, const std::function<void(const ExportProgress&)>& progress,
                                   const std::atomic<bool>* cancel) {
    const std::int64_t startedNs = HostClock::now();
    if (options.width < 16 || options.height < 16 || options.width % 2 || options.height % 2) {
        return fail(ErrorCode::InvalidArgument, "export size must be even and at least 16×16");
    }
    if (!options.frameRate.isValid()) return fail(ErrorCode::InvalidArgument, "export needs a frame rate");
    const TimeRange range = options.range.value_or(TimeRange{Time::zero(), project.timeline.duration()});
    if (range.duration <= Time::zero()) return fail(ErrorCode::InvalidArgument, "the timeline is empty");

    auto snapshot = std::make_shared<const project::Project>(project);
    FrameProvider frames(projectDir, true);
    frames.setProject(snapshot);
    AudioMixer mixer(projectDir);
    mixer.setProject(snapshot);
    const std::unique_ptr<FrameRenderer> renderer = makeRenderer();
    renderer->setProjectDirectory(projectDir);
    LEC_INFO("export", "rendering with the {} renderer", renderer->name());

    media::VideoEncoderConfig vcfg;
    vcfg.width = options.width;
    vcfg.height = options.height;
    vcfg.frameRate = options.frameRate;
    vcfg.bitRate = exportBitrate(options.width, options.height, options.frameRate, options.quality, options.bitrateScale);
    vcfg.gopFrames = std::max(1, static_cast<int>(std::lround(options.frameRate.toDouble() * 2)));
    vcfg.realtime = false;
    vcfg.preference = options.hardwareEncoder ? media::EncoderPreference::Auto : media::EncoderPreference::SoftwareOnly;
    // Frames are converted to BT.709 NV12 here, so colors never depend on an
    // encoder's own (often undocumented) RGB conversion.
    vcfg.inputFormat = AV_PIX_FMT_NV12;
    auto video = media::VideoEncoder::create(vcfg);
    if (!video) return fail(std::move(video).error());
    auto audio = media::AudioEncoder::create({media::AudioCodec::Aac, AudioMixer::kSampleRate, AudioMixer::kChannels, 16, 192'000});
    if (!audio) return fail(std::move(audio).error());

    media::MuxerOptions mopts = muxerOptionsForExport(options.output, options.container);
    mopts.metadata = {{"title", project.title}, {"encoder", std::string(LECTERN_PRODUCT_NAME) + " " + LECTERN_VERSION}};
    std::filesystem::path partial = options.output;
    partial += ".partial";
    std::error_code ec;
    std::filesystem::create_directories(options.output.parent_path(), ec);
    auto mux = media::Muxer::create(partial, mopts);
    if (!mux) return fail(std::move(mux).error());
    auto abandon = [&] {
        (*mux)->abandon();
        std::filesystem::remove(partial, ec);
    };
    auto vIndex = (*mux)->addStream((*video)->context());
    auto aIndex = (*mux)->addStream((*audio)->context());
    if (!vIndex || !aIndex) {
        abandon();
        return fail(!vIndex ? std::move(vIndex).error() : std::move(aIndex).error());
    }
    if (auto st = (*mux)->writeHeader(); !st) {
        abandon();
        return fail(std::move(st).error());
    }
    const AVRational vtb = (*video)->timeBase();
    const AVRational atb = (*audio)->timeBase();
    auto videoSink = [&](media::Packet&& p) { return (*mux)->write(std::move(p), *vIndex, vtb); };
    auto audioSink = [&](media::Packet&& p) { return (*mux)->write(std::move(p), *aIndex, atb); };

    // The canvas keeps its aspect inside the export frame.
    const double canvasAspect = static_cast<double>(snapshot->canvas.width) / std::max(1, snapshot->canvas.height);
    int renderW = options.width;
    int renderH = static_cast<int>(std::lround(options.width / canvasAspect)) & ~1;
    if (renderH > options.height) {
        renderH = options.height;
        renderW = static_cast<int>(std::lround(options.height * canvasAspect)) & ~1;
    }
    QImage canvas(renderW, renderH, QImage::Format_RGB32);
    QImage frameImage(options.width, options.height, QImage::Format_RGB32);
    auto pool = media::VideoFramePool::create(options.width, options.height, AV_PIX_FMT_NV12);
    if (!pool) {
        abandon();
        return fail(std::move(pool).error());
    }
    RgbToNv12 toNv12;
    bool direct = renderW == options.width && renderH == options.height;  // no letterbox: NV12 straight from the renderer
    // Decode ahead: the next frame's media is decoded on a worker thread while
    // this frame renders and encodes (decoding dominates export time). The
    // FrameProvider is only ever used by one thread at a time.
    struct Decoded {
        RenderPlan plan;
        std::vector<QImage> images;  ///< per plan layer (media layers only)
    };
    const auto decode = [&frames, &snapshot, renderW, renderH](Time t) {
        Decoded d{buildRenderPlan(*snapshot, t), {}};
        d.images.resize(d.plan.layers.size());
        for (std::size_t li = 0; li < d.plan.layers.size(); ++li) {
            const VisualLayer& l = d.plan.layers[li];
            const QSizeF box(l.box.w * renderW, l.box.h * renderH);  // as the renderers compute it
            if (l.kind == LayerKind::Media && box.width() >= 1 && box.height() >= 1 && l.opacity > 0) {
                d.images[li] = frames.image(l, box);
            }
        }
        return d;
    };

    const std::int64_t frameCount =
        std::max<std::int64_t>(1, options.frameRate.frameIndexAt(range.duration, Rounding::Ceil));
    const std::int64_t firstSample = range.start.toSamples(AudioMixer::kSampleRate);
    const std::int64_t totalSamples = range.duration.toSamples(AudioMixer::kSampleRate);
    std::int64_t samplesDone = 0;
    std::vector<float> pcm;
    auto encodeAudioUntil = [&](std::int64_t until) -> Status {
        until = std::min(until, totalSamples);
        while (samplesDone < until) {
            const int n = static_cast<int>(std::min<std::int64_t>(4096, until - samplesDone));
            pcm.resize(static_cast<std::size_t>(n) * AudioMixer::kChannels);
            LEC_TRY(mixer.mix(firstSample + samplesDone, n, pcm.data()));
            LEC_TRY((*audio)->encode(pcm.data(), n, audioSink));
            samplesDone += n;
        }
        return ok();
    };

    std::future<Decoded> ahead = std::async(std::launch::async, decode, range.start + options.frameRate.frameStart(0));
    for (std::int64_t i = 0; i < frameCount; ++i) {
        Decoded current = ahead.get();
        if (i + 1 < frameCount) {
            ahead = std::async(std::launch::async, decode, range.start + options.frameRate.frameStart(i + 1));
        }
        const RenderPlan& plan = current.plan;
        const FrameRenderer::ImageSource images = [&current](const VisualLayer& l, QSizeF) {
            const auto index = static_cast<std::size_t>(&l - current.plan.layers.data());
            return index < current.images.size() ? current.images[index] : QImage();
        };
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            abandon();
            return fail(ErrorCode::Cancelled, "export cancelled");
        }
        const Time t = range.start + options.frameRate.frameStart(i);
        Result<media::Frame> frame = fail(ErrorCode::Unsupported, "not rendered");
        if (direct) {
            // GPU: render straight to the encoder's NV12 (no RGB readback, no CPU conversion).
            auto target = (*pool)->acquire();
            if (!target) {
                abandon();
                return fail(std::move(target).error());
            }
            AVFrame* f = target->get();
            if (renderer->renderNv12(plan, QSize(options.width, options.height), f->data[0], f->linesize[0], f->data[1],
                                     f->linesize[1], images)) {
                f->color_range = AVCOL_RANGE_MPEG;
                f->colorspace = AVCOL_SPC_BT709;
                f->color_primaries = AVCOL_PRI_BT709;
                f->color_trc = AVCOL_TRC_BT709;
                frame = std::move(*target);
            } else {
                direct = false;  // this renderer cannot: RGB + conversion from now on
            }
        }
        if (!frame) {
            renderer->render(plan, canvas, images);
            const QImage* out = &canvas;
            if (canvas.size() != frameImage.size()) {
                frameImage.fill(Qt::black);
                QPainter painter(&frameImage);
                painter.drawImage((options.width - renderW) / 2, (options.height - renderH) / 2, canvas);
                out = &frameImage;
            }
            frame = toNv12.convert(*out, **pool);
        }
        if (!frame) {
            abandon();
            return fail(std::move(frame).error());
        }
        (*frame)->pts = i;
        if (auto st = (*video)->encode(std::move(*frame), false, videoSink); !st) {
            abandon();
            return fail(std::move(st).error());
        }
        const Time next = options.frameRate.frameStart(i + 1);
        if (auto st = encodeAudioUntil(next.toSamples(AudioMixer::kSampleRate)); !st) {
            abandon();
            return fail(std::move(st).error());
        }
        if (progress && (i % 5 == 0 || i + 1 == frameCount)) {
            const double elapsed = static_cast<double>(HostClock::now() - startedNs) / 1e9;
            progress({static_cast<double>(i + 1) / static_cast<double>(frameCount), t,
                      elapsed > 0 ? next.toSecondsF() / elapsed : 0});
        }
    }
    Status tail = encodeAudioUntil(totalSamples);
    if (tail) tail = (*video)->flush(videoSink);
    if (tail) tail = (*audio)->flush(audioSink);
    if (tail) tail = (*mux)->finalize();
    if (!tail) {
        abandon();
        return fail(std::move(tail).error());
    }
    std::filesystem::rename(partial, options.output, ec);
    if (ec) {
        std::filesystem::remove(partial, ec);
        return fail(ErrorCode::IoError, "cannot move the export into place: " + ec.message());
    }

    ExportResult result;
    result.path = options.output;
    result.duration = range.duration;
    result.frames = frameCount;
    result.bytes = static_cast<std::uint64_t>(std::filesystem::file_size(options.output, ec));
    result.videoEncoder = (*video)->identity().name;
    result.hardware = (*video)->identity().hardware;
    result.seconds = static_cast<double>(HostClock::now() - startedNs) / 1e9;
    LEC_INFO("editor", "exported {} ({} frames, {:.1f} MB) with {} in {:.1f} s", options.output.filename().string(),
             frameCount, static_cast<double>(result.bytes) / 1e6, result.videoEncoder, result.seconds);
    return result;
}

}  // namespace lectern::editor
