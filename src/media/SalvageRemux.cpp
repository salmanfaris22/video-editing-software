#include "media/SalvageRemux.h"

#include "media/Muxer.h"

#include <limits>
#include <vector>

namespace lectern::media {

Result<SalvageResult> salvageRemux(const std::filesystem::path& input, const std::filesystem::path& output) {
    AVFormatContext* raw = nullptr;
    const std::string inStr = input.string();
    Dictionary opts;
    opts.set("probesize", 8 << 20);
    if (const int ret = avformat_open_input(&raw, inStr.c_str(), nullptr, opts.address()); ret < 0) {
        return fail(ffError(ret, "open " + inStr, ErrorCode::Corrupt));
    }
    AVFormatInputPtr in(raw);
    // Truncated files may make stream info incomplete; codec parameters from
    // the header (CodecPrivate) are what matter for remuxing.
    (void)avformat_find_stream_info(in.get(), nullptr);
    if (in->nb_streams == 0) return fail(ErrorCode::Corrupt, "no streams in " + inStr);

    MuxerOptions mopts = recordingMuxerOptions();
    auto muxer = Muxer::create(output, mopts);
    if (!muxer) return fail(std::move(muxer).error());

    std::vector<int> mapping(in->nb_streams, -1);
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        const AVStream* st = in->streams[i];
        const auto type = st->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO) continue;
        auto idx = (*muxer)->addStream(*st->codecpar, st->time_base);
        if (!idx) return fail(std::move(idx).error());
        mapping[i] = *idx;
    }
    LEC_TRY((*muxer)->writeHeader());

    SalvageResult result;
    std::vector<std::int64_t> lastDts(in->nb_streams, std::numeric_limits<std::int64_t>::min());
    bool haveFirst = false;
    for (;;) {
        Packet pkt = Packet::alloc();
        if (!pkt) return fail(ErrorCode::OutOfMemory, "av_packet_alloc");
        const int ret = av_read_frame(in.get(), pkt.get());
        if (ret == AVERROR_EOF) break;
        if (ret < 0) {
            result.inputWasTruncated = true;
            LEC_INFO("media", "salvage of {} stopped at read error: {}", inStr, ffErrorString(ret));
            break;
        }
        const int si = pkt->stream_index;
        if (si < 0 || static_cast<unsigned>(si) >= in->nb_streams || mapping[static_cast<std::size_t>(si)] < 0) continue;
        const AVRational tb = in->streams[si]->time_base;
        if ((pkt->flags & AV_PKT_FLAG_CORRUPT) || pkt->size <= 0 || pkt->pts == AV_NOPTS_VALUE) {
            ++result.packetsDropped;
            continue;
        }
        const std::int64_t dts = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
        if (dts < lastDts[static_cast<std::size_t>(si)]) {
            ++result.packetsDropped;
            continue;
        }
        lastDts[static_cast<std::size_t>(si)] = dts;

        const Time pts = Time::fromRational(pkt->pts, fromAV(tb));
        const Time end = pts + Time::fromRational(pkt->duration > 0 ? pkt->duration : 0, fromAV(tb));
        if (!haveFirst || pts < result.firstTimestamp) result.firstTimestamp = pts;
        haveFirst = true;
        if (end > result.endTimestamp) result.endTimestamp = end;

        LEC_TRY((*muxer)->write(std::move(pkt), mapping[static_cast<std::size_t>(si)], tb));
        ++result.packetsCopied;
    }
    if (result.packetsCopied == 0) {
        (*muxer)->abandon();
        std::error_code ec;
        std::filesystem::remove(output, ec);
        return fail(ErrorCode::Corrupt, "no recoverable packets in " + inStr);
    }
    LEC_TRY((*muxer)->finalize());
    result.bytesWritten = (*muxer)->bytesWritten();
    return result;
}

}  // namespace lectern::media
