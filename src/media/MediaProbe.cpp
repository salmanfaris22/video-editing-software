#include "media/MediaProbe.h"

#include "media/Frame.h"

#include <cmath>
#include <set>

namespace lectern::media {

std::string_view toString(StreamType type) noexcept {
    switch (type) {
        case StreamType::Video: return "video";
        case StreamType::Audio: return "audio";
        case StreamType::Subtitle: return "subtitle";
        case StreamType::Data: return "data";
        case StreamType::Attachment: return "attachment";
        case StreamType::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view toString(MediaKind kind) noexcept {
    switch (kind) {
        case MediaKind::Video: return "video";
        case MediaKind::Audio: return "audio";
        case MediaKind::Image: return "image";
        case MediaKind::Unknown: return "unknown";
    }
    return "unknown";
}

namespace {

StreamType streamType(AVMediaType t) {
    switch (t) {
        case AVMEDIA_TYPE_VIDEO: return StreamType::Video;
        case AVMEDIA_TYPE_AUDIO: return StreamType::Audio;
        case AVMEDIA_TYPE_SUBTITLE: return StreamType::Subtitle;
        case AVMEDIA_TYPE_DATA: return StreamType::Data;
        case AVMEDIA_TYPE_ATTACHMENT: return StreamType::Attachment;
        default: return StreamType::Unknown;
    }
}

std::string nameOr(const char* s, const char* fallback = "") { return s ? s : fallback; }

int rotationOf(const AVCodecParameters* par) {
    const AVPacketSideData* sd =
        av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < 9 * 4) return 0;
    const double angle = av_display_rotation_get(reinterpret_cast<const std::int32_t*>(sd->data));
    if (std::isnan(angle)) return 0;
    // av_display_rotation_get returns counter-clockwise degrees; convert to clockwise.
    long r = std::lround(-angle) % 360;
    if (r < 0) r += 360;
    return static_cast<int>((r + 45) / 90 * 90 % 360);
}

bool isImageFormat(const AVFormatContext* fmt) {
    static const std::set<std::string> kImageDemuxers = {
        "image2", "png_pipe", "jpeg_pipe", "webp_pipe", "bmp_pipe", "tiff_pipe", "gif", "jpegxl_pipe",
        "dpx_pipe", "exr_pipe", "ppm_pipe", "pgm_pipe", "psd_pipe", "svg_pipe", "qoi_pipe", "j2k_pipe"};
    return fmt->iformat && kImageDemuxers.contains(fmt->iformat->name);
}

}  // namespace

Result<MediaInfo> probeMedia(const std::filesystem::path& path, const ProbeOptions& options) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return fail(ErrorCode::NotFound, "no such file: " + path.string());

    Dictionary opts;
    opts.set("probesize", options.probeSizeBytes);
    opts.set("analyzeduration", options.analyzeDurationUs);
    AVFormatContext* raw = nullptr;
    const std::string pathStr = path.string();
    if (const int ret = avformat_open_input(&raw, pathStr.c_str(), nullptr, opts.address()); ret < 0) {
        return fail(ffError(ret, "open " + pathStr, ErrorCode::Unsupported));
    }
    AVFormatInputPtr fmt(raw);
    if (const int ret = avformat_find_stream_info(fmt.get(), nullptr); ret < 0) {
        return fail(ffError(ret, "read stream info " + pathStr, ErrorCode::Corrupt));
    }

    MediaInfo info;
    info.path = pathStr;
    info.container = nameOr(fmt->iformat->name);
    info.containerLongName = nameOr(fmt->iformat->long_name);
    info.fileSize = std::filesystem::file_size(path, ec);
    info.bitRate = fmt->bit_rate;
    if (fmt->start_time != AV_NOPTS_VALUE) info.start = Time::fromMicroseconds(fmt->start_time);
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) info.duration = Time::fromMicroseconds(fmt->duration);

    const AVDictionaryEntry* tag = nullptr;
    while ((tag = av_dict_get(fmt->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) info.tags[tag->key] = tag->value;

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream* st = fmt->streams[i];
        const AVCodecParameters* par = st->codecpar;
        StreamInfo s;
        s.index = static_cast<int>(i);
        s.type = streamType(par->codec_type);
        s.codec = nameOr(avcodec_get_name(par->codec_id), "unknown");
        if (const AVCodecDescriptor* d = avcodec_descriptor_get(par->codec_id)) s.codecLongName = nameOr(d->long_name);
        s.profile = nameOr(avcodec_profile_name(par->codec_id, par->profile));
        s.bitRate = par->bit_rate;
        s.isAttachedPicture = (st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        s.isDefault = (st->disposition & AV_DISPOSITION_DEFAULT) != 0;
        if (st->start_time != AV_NOPTS_VALUE) s.start = Time::fromRational(st->start_time, fromAV(st->time_base));
        if (st->duration != AV_NOPTS_VALUE && st->duration > 0) {
            s.duration = Time::fromRational(st->duration, fromAV(st->time_base));
        } else {
            s.duration = info.duration;
        }
        if (const AVDictionaryEntry* lang = av_dict_get(st->metadata, "language", nullptr, 0)) s.language = lang->value;

        if (s.type == StreamType::Video) {
            VideoStreamInfo v;
            v.width = par->width;
            v.height = par->height;
            if (par->sample_aspect_ratio.num > 0) v.sampleAspectRatio = fromAV(par->sample_aspect_ratio);
            if (st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0) {
                v.averageFrameRate = FrameRate(fromAV(st->avg_frame_rate));
            }
            if (st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0) {
                v.realFrameRate = FrameRate(fromAV(st->r_frame_rate));
            }
            if (v.averageFrameRate.isValid() && v.realFrameRate.isValid()) {
                const double a = v.averageFrameRate.toDouble();
                const double r = v.realFrameRate.toDouble();
                v.variableFrameRate = std::fabs(a - r) / r > 0.01;
            }
            v.rotationDegrees = rotationOf(par);
            const auto pixFmt = static_cast<AVPixelFormat>(par->format);
            v.pixelFormat = pixelFormatName(pixFmt);
            if (const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pixFmt)) {
                v.bitDepth = desc->comp[0].depth;
                v.hasAlpha = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
            }
            v.colorSpace = nameOr(av_color_space_name(par->color_space));
            v.colorPrimaries = nameOr(av_color_primaries_name(par->color_primaries));
            v.colorTransfer = nameOr(av_color_transfer_name(par->color_trc));
            v.colorRange = nameOr(av_color_range_name(par->color_range));
            v.hdr = par->color_trc == AVCOL_TRC_SMPTE2084 || par->color_trc == AVCOL_TRC_ARIB_STD_B67;
            v.frameCount = st->nb_frames;
            s.video = v;
        } else if (s.type == StreamType::Audio) {
            AudioStreamInfo a;
            a.sampleRate = par->sample_rate;
            a.channels = par->ch_layout.nb_channels;
            char layout[128] = {};
            av_channel_layout_describe(&par->ch_layout, layout, sizeof layout);
            a.channelLayout = layout;
            a.sampleFormat = sampleFormatName(static_cast<AVSampleFormat>(par->format));
            a.bitDepth = par->bits_per_raw_sample > 0 ? par->bits_per_raw_sample
                                                      : av_get_bytes_per_sample(static_cast<AVSampleFormat>(par->format)) * 8;
            s.audio = a;
        }
        info.streams.push_back(std::move(s));
    }

    const int bestVideo = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int bestAudio = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (bestVideo >= 0 && !info.streams[static_cast<std::size_t>(bestVideo)].isAttachedPicture) {
        info.bestVideoStream = bestVideo;
    }
    if (bestAudio >= 0) info.bestAudioStream = bestAudio;

    if (isImageFormat(fmt.get()) && info.bestVideoStream >= 0) {
        info.kind = MediaKind::Image;
    } else if (info.bestVideoStream >= 0) {
        info.kind = MediaKind::Video;
    } else if (info.bestAudioStream >= 0) {
        info.kind = MediaKind::Audio;
    }
    if (info.kind == MediaKind::Unknown) {
        return fail(ErrorCode::Unsupported, "no usable audio or video stream in " + pathStr);
    }
    return info;
}

json::Json toJson(const MediaInfo& info) {
    json::Json streams = json::Json::array();
    for (const auto& s : info.streams) {
        json::Json js{{"index", s.index},
                      {"type", std::string(toString(s.type))},
                      {"codec", s.codec},
                      {"profile", s.profile},
                      {"bitRate", s.bitRate},
                      {"startSeconds", s.start.toSecondsF()},
                      {"durationSeconds", s.duration.toSecondsF()},
                      {"attachedPicture", s.isAttachedPicture}};
        if (s.video) {
            const auto& v = *s.video;
            js["video"] = {{"width", v.width},
                           {"height", v.height},
                           {"avgFrameRate", v.averageFrameRate.rational().toString()},
                           {"realFrameRate", v.realFrameRate.rational().toString()},
                           {"vfr", v.variableFrameRate},
                           {"rotation", v.rotationDegrees},
                           {"pixelFormat", v.pixelFormat},
                           {"bitDepth", v.bitDepth},
                           {"colorSpace", v.colorSpace},
                           {"colorPrimaries", v.colorPrimaries},
                           {"colorTransfer", v.colorTransfer},
                           {"colorRange", v.colorRange},
                           {"hdr", v.hdr},
                           {"frames", v.frameCount}};
        }
        if (s.audio) {
            const auto& a = *s.audio;
            js["audio"] = {{"sampleRate", a.sampleRate},
                           {"channels", a.channels},
                           {"layout", a.channelLayout},
                           {"sampleFormat", a.sampleFormat},
                           {"bitDepth", a.bitDepth}};
        }
        streams.push_back(std::move(js));
    }
    return json::Json{{"path", info.path},
                      {"container", info.container},
                      {"kind", std::string(toString(info.kind))},
                      {"startSeconds", info.start.toSecondsF()},
                      {"durationSeconds", info.duration.toSecondsF()},
                      {"bitRate", info.bitRate},
                      {"fileSize", info.fileSize},
                      {"tags", info.tags},
                      {"streams", std::move(streams)}};
}

}  // namespace lectern::media
