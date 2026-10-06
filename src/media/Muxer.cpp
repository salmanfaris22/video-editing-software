#include "media/Muxer.h"

namespace lectern::media {

MuxerOptions recordingMuxerOptions() {
    MuxerOptions o;
    o.format = "matroska";
    o.formatOptions = {
        {"cluster_time_limit", "1000"},
        {"cluster_size_limit", "4194304"},
        {"write_crc32", "1"},
    };
    return o;
}

Result<std::unique_ptr<Muxer>> Muxer::create(const std::filesystem::path& path, const MuxerOptions& options) {
    std::unique_ptr<Muxer> m(new Muxer());
    m->path_ = path;
    m->options_ = options;

    const std::string pathStr = path.string();
    if (const int ret = avformat_alloc_output_context2(&m->format_, nullptr, options.format.c_str(), pathStr.c_str());
        ret < 0 || !m->format_) {
        return fail(ffError(ret < 0 ? ret : AVERROR(EINVAL), "create " + options.format + " muxer"));
    }
    auto sink = FileSink::create(path, options.sink);
    if (!sink) return fail(std::move(sink).error());
    m->sink_ = std::move(*sink);
    m->format_->pb = m->sink_->avio();
    m->format_->flags |= AVFMT_FLAG_CUSTOM_IO;
    for (const auto& [key, value] : options.metadata) av_dict_set(&m->format_->metadata, key.c_str(), value.c_str(), 0);
    return m;
}

Muxer::~Muxer() {
    if (!closed_) abandon();
}

Error Muxer::mapWriteError(int ret, std::string_view what) const {
    // Prefer the errno observed by the sink: it distinguishes ENOSPC from EIO.
    if (sink_ && sink_->lastErrno() != 0) {
        return errnoError(errorCodeFromErrno(sink_->lastErrno()), std::string(what) + " " + path_.string(),
                          sink_->lastErrno());
    }
    return ffError(ret, std::string(what) + " " + path_.string(), ErrorCode::IoError);
}

Result<int> Muxer::addStream(const AVCodecContext& encoder) {
    if (headerWritten_) return fail(ErrorCode::InvalidState, "addStream after writeHeader");
    AVStream* st = avformat_new_stream(format_, nullptr);
    if (!st) return fail(ErrorCode::OutOfMemory, "avformat_new_stream");
    if (const int ret = avcodec_parameters_from_context(st->codecpar, &encoder); ret < 0) {
        return fail(ffError(ret, "copy encoder parameters"));
    }
    st->time_base = encoder.time_base;
    if (encoder.codec_type == AVMEDIA_TYPE_VIDEO) st->avg_frame_rate = encoder.framerate;
    return st->index;
}

Result<int> Muxer::addStream(const AVCodecParameters& parameters, AVRational timeBase) {
    if (headerWritten_) return fail(ErrorCode::InvalidState, "addStream after writeHeader");
    AVStream* st = avformat_new_stream(format_, nullptr);
    if (!st) return fail(ErrorCode::OutOfMemory, "avformat_new_stream");
    if (const int ret = avcodec_parameters_copy(st->codecpar, &parameters); ret < 0) {
        return fail(ffError(ret, "copy codec parameters"));
    }
    st->codecpar->codec_tag = 0;  // let the target container choose
    st->time_base = timeBase;
    return st->index;
}

Status Muxer::writeHeader() {
    if (headerWritten_) return ok();
    Dictionary opts;
    for (const auto& [key, value] : options_.formatOptions) opts.set(key.c_str(), value.c_str());
    if (const int ret = avformat_write_header(format_, opts.address()); ret < 0) {
        return fail(mapWriteError(ret, "write header"));
    }
    headerWritten_ = true;
    return flush();
}

Status Muxer::write(Packet&& packet, int streamIndex, AVRational sourceTimeBase) {
    if (!headerWritten_) return fail(ErrorCode::InvalidState, "write before writeHeader");
    if (closed_) return fail(ErrorCode::InvalidState, "write after close");
    if (streamIndex < 0 || static_cast<unsigned>(streamIndex) >= format_->nb_streams) {
        return fail(ErrorCode::InvalidArgument, "bad stream index");
    }
    AVPacket* pkt = packet.get();
    pkt->stream_index = streamIndex;
    av_packet_rescale_ts(pkt, sourceTimeBase, format_->streams[streamIndex]->time_base);
    // av_interleaved_write_frame takes ownership of the packet's data.
    if (const int ret = av_interleaved_write_frame(format_, pkt); ret < 0) {
        return fail(mapWriteError(ret, "write packet"));
    }
    return ok();
}

Status Muxer::flush() {
    if (!sink_) return ok();
    if (auto st = sink_->flushBuffer(); !st) {
        if (sink_->lastErrno() != 0) return fail(mapWriteError(AVERROR(sink_->lastErrno()), "flush"));
        return st;
    }
    return ok();
}

Status Muxer::sync(bool full) {
    LEC_TRY(flush());
    return sink_ ? sink_->sync(full) : ok();
}

Status Muxer::finalize() {
    if (closed_) return ok();
    Status result = ok();
    if (headerWritten_) {
        // Drain interleaving queue, then trailer (Matroska: Cues + Duration).
        if (const int ret = av_interleaved_write_frame(format_, nullptr); ret < 0) {
            result = fail(mapWriteError(ret, "flush interleaver"));
        }
        if (const int ret = av_write_trailer(format_); ret < 0 && result) {
            result = fail(mapWriteError(ret, "write trailer"));
        }
    }
    if (sink_) {
        finalBytes_ = sink_->bytesWritten();
        Status closeStatus = sink_->close(true);
        if (result && !closeStatus) result = std::move(closeStatus);
        sink_.reset();
    }
    format_->pb = nullptr;
    avformat_free_context(format_);
    format_ = nullptr;
    closed_ = true;
    return result;
}

void Muxer::abandon() {
    if (closed_) return;
    if (sink_) {
        finalBytes_ = sink_->bytesWritten();
        (void)sink_->close(false);
        sink_.reset();
    }
    if (format_) {
        format_->pb = nullptr;
        avformat_free_context(format_);
        format_ = nullptr;
    }
    closed_ = true;
}

AVRational Muxer::streamTimeBase(int index) const {
    if (!format_ || index < 0 || static_cast<unsigned>(index) >= format_->nb_streams) return AVRational{0, 1};
    return format_->streams[index]->time_base;
}

}  // namespace lectern::media
