#include "media/VideoEncoder.h"

#include <algorithm>
#include <array>

namespace lectern::media {

std::string_view toString(VideoCodec codec) noexcept { return codec == VideoCodec::HEVC ? "hevc" : "h264"; }

std::string_view toString(EncoderPreference preference) noexcept {
    switch (preference) {
        case EncoderPreference::Auto: return "auto";
        case EncoderPreference::HardwareOnly: return "hardware";
        case EncoderPreference::SoftwareOnly: return "software";
    }
    return "auto";
}

EncoderPreference encoderPreferenceFromString(std::string_view text) noexcept {
    if (text == "hardware") return EncoderPreference::HardwareOnly;
    if (text == "software") return EncoderPreference::SoftwareOnly;
    return EncoderPreference::Auto;
}

std::vector<std::string> videoEncoderCandidates(VideoCodec codec, EncoderPreference preference) {
    const bool hevc = codec == VideoCodec::HEVC;
    std::vector<std::string> hardware;
#if defined(__APPLE__)
    hardware = {hevc ? "hevc_videotoolbox" : "h264_videotoolbox"};
#elif defined(_WIN32)
    hardware = hevc ? std::vector<std::string>{"hevc_nvenc", "hevc_qsv", "hevc_amf", "hevc_mf"}
                    : std::vector<std::string>{"h264_nvenc", "h264_qsv", "h264_amf", "h264_mf"};
#else
    hardware = hevc ? std::vector<std::string>{"hevc_vaapi", "hevc_nvenc", "hevc_qsv"}
                    : std::vector<std::string>{"h264_vaapi", "h264_nvenc", "h264_qsv"};
#endif
    const std::vector<std::string> software =
        hevc ? std::vector<std::string>{"libx265"} : std::vector<std::string>{"libx264", "libopenh264"};

    std::vector<std::string> out;
    if (preference != EncoderPreference::SoftwareOnly) out.insert(out.end(), hardware.begin(), hardware.end());
    if (preference != EncoderPreference::HardwareOnly) out.insert(out.end(), software.begin(), software.end());
    return out;
}

bool isHardwareEncoder(const AVCodec& codec) noexcept {
    if (codec.capabilities & AV_CODEC_CAP_HARDWARE) return true;
    const std::string_view name = codec.name;
    for (std::string_view suffix : {"_videotoolbox", "_nvenc", "_qsv", "_amf", "_vaapi", "_mf", "_v4l2m2m"}) {
        if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix) return true;
    }
    return false;
}

namespace {

std::vector<AVPixelFormat> supportedPixelFormats(const AVCodecContext* ctx, const AVCodec* codec) {
    const void* configs = nullptr;
    int count = 0;
    std::vector<AVPixelFormat> out;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count) >= 0 && configs) {
        const auto* fmts = static_cast<const AVPixelFormat*>(configs);
        out.assign(fmts, fmts + count);
    }
    return out;  // empty = unknown/any
}

bool contains(const std::vector<AVPixelFormat>& v, AVPixelFormat f) {
    return std::find(v.begin(), v.end(), f) != v.end();
}

AVHWDeviceType hwDeviceTypeFor(AVPixelFormat fmt) {
    switch (fmt) {
        case AV_PIX_FMT_VIDEOTOOLBOX: return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
        case AV_PIX_FMT_VAAPI: return AV_HWDEVICE_TYPE_VAAPI;
        case AV_PIX_FMT_CUDA: return AV_HWDEVICE_TYPE_CUDA;
        case AV_PIX_FMT_QSV: return AV_HWDEVICE_TYPE_QSV;
        case AV_PIX_FMT_D3D11: return AV_HWDEVICE_TYPE_D3D11VA;
        default: return AV_HWDEVICE_TYPE_NONE;
    }
}

void applyEncoderOptions(Dictionary& d, std::string_view name, const VideoEncoderConfig& cfg) {
    const bool h264 = cfg.codec == VideoCodec::H264;
    if (name.ends_with("_videotoolbox")) {
        d.set("realtime", cfg.realtime ? "1" : "0");
        d.set("allow_sw", "0");  // software fallback is ours, not VideoToolbox's
        d.set("profile", h264 ? "high" : "main");
        if (cfg.realtime) d.set("prio_speed", "1");
    } else if (name == "libx264" || name == "libx265") {
        d.set("preset", cfg.realtime ? "veryfast" : "medium");
        if (name == "libx265") d.set("x265-params", "log-level=error");
    } else if (name.ends_with("_nvenc")) {
        d.set("preset", "p4");
        d.set("tune", "hq");
        d.set("rc", "vbr");
    } else if (name.ends_with("_qsv")) {
        d.set("preset", cfg.realtime ? "veryfast" : "medium");
        d.set("async_depth", "4");
    } else if (name.ends_with("_amf")) {
        d.set("usage", "transcoding");
        d.set("quality", cfg.realtime ? "speed" : "balanced");
    } else if (name.ends_with("_mf")) {
        d.set("hw_encoding", "1");
    } else if (name.ends_with("_vaapi")) {
        d.set("rc_mode", "VBR");
    } else if (name == "libopenh264") {
        d.set("allow_skip_frames", "0");
    }
}

}  // namespace

Result<std::unique_ptr<VideoEncoder>> VideoEncoder::create(const VideoEncoderConfig& config) {
    if (config.width <= 0 || config.height <= 0 || (config.width % 2) || (config.height % 2)) {
        return fail(ErrorCode::InvalidArgument, "video encoder needs positive even dimensions, got " +
                                                    std::to_string(config.width) + "x" + std::to_string(config.height));
    }
    if (!config.frameRate.isValid()) return fail(ErrorCode::InvalidArgument, "video encoder needs a frame rate");

    const std::vector<std::string> candidates =
        config.encoderNames.empty() ? videoEncoderCandidates(config.codec, config.preference) : config.encoderNames;
    const bool inputIsHw = isHardwarePixelFormat(config.inputFormat);
    const AVPixelFormat inputSw = inputIsHw ? config.inputSwFormat : config.inputFormat;

    std::string attempts;
    for (const std::string& name : candidates) {
        const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec) {
            attempts += name + ": not built; ";
            continue;
        }
        AVCodecContextPtr ctx(avcodec_alloc_context3(codec));
        if (!ctx) return fail(ErrorCode::OutOfMemory, "avcodec_alloc_context3");

        ctx->width = config.width;
        ctx->height = config.height;
        ctx->time_base = toAV(config.frameRate.frameTimeBase());
        ctx->framerate = toAV(config.frameRate.rational());
        ctx->gop_size = std::max(1, config.gopFrames);
        ctx->max_b_frames = 0;  // see RECORDING_ENGINE.md §5.4
        ctx->bit_rate = config.bitRate;
        ctx->color_primaries = config.color.primaries;
        ctx->color_trc = config.color.transfer;
        ctx->colorspace = config.color.space;
        ctx->color_range = config.color.range;
        ctx->sample_aspect_ratio = AVRational{1, 1};
        ctx->thread_count = 0;
        if (config.globalHeader) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // Pixel format negotiation (fewest copies wins).
        const std::vector<AVPixelFormat> supported = supportedPixelFormats(ctx.get(), codec);
        bool zeroCopy = false;
        bool needsUpload = false;
        if (inputIsHw && contains(supported, config.inputFormat)) {
            ctx->pix_fmt = config.inputFormat;
            ctx->sw_pix_fmt = inputSw;
            if (config.inputHwFramesCtx) ctx->hw_frames_ctx = av_buffer_ref(config.inputHwFramesCtx);
            zeroCopy = true;
        } else if (supported.empty() || contains(supported, inputSw)) {
            ctx->pix_fmt = inputSw != AV_PIX_FMT_NONE ? inputSw : AV_PIX_FMT_YUV420P;
        } else {
            AVPixelFormat chosen = AV_PIX_FMT_NONE;
            for (AVPixelFormat pref : {AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P}) {
                if (contains(supported, pref)) {
                    chosen = pref;
                    break;
                }
            }
            if (chosen == AV_PIX_FMT_NONE) {
                for (AVPixelFormat f : supported) {
                    if (!isHardwarePixelFormat(f)) {
                        chosen = f;
                        break;
                    }
                }
            }
            if (chosen == AV_PIX_FMT_NONE) {
                // Hardware-only input (e.g. VA-API): upload software frames.
                chosen = supported.front();
                needsUpload = true;
            }
            ctx->pix_fmt = chosen;
        }

        BufferRef uploadCtx;
        if (needsUpload) {
            const AVHWDeviceType type = hwDeviceTypeFor(ctx->pix_fmt);
            AVBufferRef* device = nullptr;
            if (type == AV_HWDEVICE_TYPE_NONE || av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0) {
                attempts += name + ": no hardware device; ";
                continue;
            }
            BufferRef deviceRef = BufferRef::adopt(device);
            BufferRef frames = BufferRef::adopt(av_hwframe_ctx_alloc(deviceRef.get()));
            if (!frames) {
                attempts += name + ": frames ctx alloc failed; ";
                continue;
            }
            auto* fc = reinterpret_cast<AVHWFramesContext*>(frames.get()->data);
            fc->format = ctx->pix_fmt;
            fc->sw_format = AV_PIX_FMT_NV12;
            fc->width = config.width;
            fc->height = config.height;
            fc->initial_pool_size = 8;
            if (av_hwframe_ctx_init(frames.get()) < 0) {
                attempts += name + ": frames ctx init failed; ";
                continue;
            }
            ctx->sw_pix_fmt = AV_PIX_FMT_NV12;
            ctx->hw_frames_ctx = frames.newRef();
            uploadCtx = std::move(frames);
        }

        Dictionary opts;
        applyEncoderOptions(opts, name, config);
        if (const int ret = avcodec_open2(ctx.get(), codec, opts.address()); ret < 0) {
            attempts += name + ": " + ffErrorString(ret) + "; ";
            LEC_DEBUG("media", "encoder {} failed to open: {}", name, ffErrorString(ret));
            continue;
        }

        std::unique_ptr<VideoEncoder> enc(new VideoEncoder());
        enc->ctx_ = std::move(ctx);
        enc->config_ = config;
        enc->config_.inputHwFramesCtx = nullptr;  // borrowed pointer; never used after create
        enc->uploadFramesCtx_ = std::move(uploadCtx);
        enc->identity_ = EncoderIdentity{name, isHardwareEncoder(*codec), zeroCopy, enc->ctx_->pix_fmt};
        LEC_DEBUG("media", "video encoder {} ({}, {}) {}x{} @ {} fps, {} kbit/s, input {} → {}", name,
                 enc->identity_.hardware ? "hardware" : "software", zeroCopy ? "zero-copy" : "copy", config.width,
                 config.height, config.frameRate.toDouble(), config.bitRate / 1000, pixelFormatName(config.inputFormat),
                 pixelFormatName(enc->ctx_->pix_fmt));
        return enc;
    }
    return fail(ErrorCode::EncoderError, "no usable " + std::string(toString(config.codec)) +
                                             " encoder (" + std::string(toString(config.preference)) + "): " + attempts);
}

VideoEncoder::~VideoEncoder() = default;

Result<Frame> VideoEncoder::adapt(Frame frame) {
    const auto fmt = static_cast<AVPixelFormat>(frame->format);
    if (fmt == ctx_->pix_fmt && frame->width == ctx_->width && frame->height == ctx_->height) return frame;

    Frame current = std::move(frame);

    // 1) Hardware frame into a software encoder: download once.
    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(current->format)) &&
        !isHardwarePixelFormat(ctx_->pix_fmt)) {
        Frame sw = Frame::alloc();
        if (!sw) return fail(ErrorCode::OutOfMemory, "alloc download frame");
        if (const int ret = av_hwframe_transfer_data(sw.get(), current.get(), 0); ret < 0) {
            return fail(ffError(ret, "download hardware frame", ErrorCode::EncoderError));
        }
        av_frame_copy_props(sw.get(), current.get());
        current = std::move(sw);
        if (current->format == ctx_->pix_fmt && current->width == ctx_->width && current->height == ctx_->height) {
            return current;
        }
    }

    // 2) Software conversion/scaling to the encoder's software format.
    const AVPixelFormat target = isHardwarePixelFormat(ctx_->pix_fmt) ? ctx_->sw_pix_fmt : ctx_->pix_fmt;
    if (current->format != target || current->width != ctx_->width || current->height != ctx_->height) {
        if (!convertScratch_ || convertScratch_->format != target) {
            auto scratch = Frame::allocVideo(ctx_->width, ctx_->height, target);
            if (!scratch) return fail(std::move(scratch).error());
            convertScratch_ = std::move(*scratch);
        }
        if (const int ret = av_frame_make_writable(convertScratch_.get()); ret < 0) {
            return fail(ffError(ret, "make conversion frame writable"));
        }
        sws_.reset(sws_getCachedContext(sws_.release(), current->width, current->height,
                                        static_cast<AVPixelFormat>(current->format), ctx_->width, ctx_->height, target,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!sws_) return fail(ErrorCode::EncoderError, "cannot create swscale context");
        // swscale defaults to BT.601; use the source's and the encoder's real
        // matrices and ranges so colors survive (e.g. BGRA → BT.709 NV12).
        auto coefficients = [](AVColorSpace space, int height) {
            switch (space) {
                case AVCOL_SPC_BT709: return SWS_CS_ITU709;
                case AVCOL_SPC_BT2020_NCL:
                case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
                case AVCOL_SPC_SMPTE170M:
                case AVCOL_SPC_BT470BG: return SWS_CS_ITU601;
                default: return height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
            }
        };
        const auto* srcDesc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(current->format));
        const bool srcRgb = srcDesc && (srcDesc->flags & AV_PIX_FMT_FLAG_RGB);
        const int srcRange = srcRgb || current->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        const int dstRange = ctx_->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        sws_setColorspaceDetails(sws_.get(), sws_getCoefficients(coefficients(current->colorspace, current->height)),
                                 srcRange, sws_getCoefficients(coefficients(ctx_->colorspace, ctx_->height)), dstRange, 0,
                                 1 << 16, 1 << 16);
        sws_scale(sws_.get(), current->data, current->linesize, 0, current->height, convertScratch_->data,
                  convertScratch_->linesize);
        Frame converted = convertScratch_.ref();
        if (!converted) return fail(ErrorCode::OutOfMemory, "ref conversion frame");
        av_frame_copy_props(converted.get(), current.get());
        converted->color_range = ctx_->color_range;
        converted->colorspace = ctx_->colorspace;
        current = std::move(converted);
    }

    // 3) Software frame into a hardware-only encoder: upload once.
    if (isHardwarePixelFormat(ctx_->pix_fmt) && uploadFramesCtx_) {
        Frame hw = Frame::alloc();
        if (!hw) return fail(ErrorCode::OutOfMemory, "alloc upload frame");
        if (const int ret = av_hwframe_get_buffer(uploadFramesCtx_.get(), hw.get(), 0); ret < 0) {
            return fail(ffError(ret, "get hardware upload buffer", ErrorCode::EncoderError));
        }
        if (const int ret = av_hwframe_transfer_data(hw.get(), current.get(), 0); ret < 0) {
            return fail(ffError(ret, "upload frame", ErrorCode::EncoderError));
        }
        av_frame_copy_props(hw.get(), current.get());
        current = std::move(hw);
    }
    return current;
}

Status VideoEncoder::drain(const PacketSink& sink) {
    for (;;) {
        Packet pkt = Packet::alloc();
        if (!pkt) return fail(ErrorCode::OutOfMemory, "av_packet_alloc");
        const int ret = avcodec_receive_packet(ctx_.get(), pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return ok();
        if (ret < 0) return fail(ffError(ret, "receive packet from " + identity_.name, ErrorCode::EncoderError));
        if (pkt->duration == 0) pkt->duration = 1;  // one frame in 1/fps time base
        LEC_TRY(sink(std::move(pkt)));
    }
}

Status VideoEncoder::encode(Frame frame, bool forceKeyframe, const PacketSink& sink) {
    if (flushed_) return fail(ErrorCode::InvalidState, "encode after flush");
    if (!frame) return fail(ErrorCode::InvalidArgument, "null frame");
    auto adapted = adapt(std::move(frame));
    if (!adapted) return fail(std::move(adapted).error());
    Frame toSend = std::move(*adapted);
    toSend->pict_type = forceKeyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    if (forceKeyframe) toSend->flags |= AV_FRAME_FLAG_KEY;

    int ret = avcodec_send_frame(ctx_.get(), toSend.get());
    if (ret == AVERROR(EAGAIN)) {
        LEC_TRY(drain(sink));
        ret = avcodec_send_frame(ctx_.get(), toSend.get());
    }
    if (ret < 0) return fail(ffError(ret, "send frame to " + identity_.name, ErrorCode::EncoderError));
    ++framesIn_;
    return drain(sink);
}

Status VideoEncoder::flush(const PacketSink& sink) {
    if (flushed_) return ok();
    flushed_ = true;
    if (const int ret = avcodec_send_frame(ctx_.get(), nullptr); ret < 0 && ret != AVERROR_EOF) {
        return fail(ffError(ret, "flush " + identity_.name, ErrorCode::EncoderError));
    }
    return drain(sink);
}

}  // namespace lectern::media
