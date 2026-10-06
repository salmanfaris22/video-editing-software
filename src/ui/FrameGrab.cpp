#include "ui/FrameGrab.h"

#include "media/Frame.h"

#include <list>
#include <mutex>
#include <utility>

namespace lectern::ui {

namespace {

std::mutex cacheMutex;
std::list<std::pair<QString, QImage>> cache;  // most recently used first, at most 64

QImage cached(const QString& key) {
    std::lock_guard lock(cacheMutex);
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (it->first == key) {
            cache.splice(cache.begin(), cache, it);
            return cache.front().second;
        }
    }
    return {};
}

void store(const QString& key, const QImage& image) {
    std::lock_guard lock(cacheMutex);
    cache.emplace_front(key, image);
    while (cache.size() > 64) cache.pop_back();
}

QImage decodeFrame(const QString& path, double seconds, int maxHeight) {
    AVFormatContext* raw = nullptr;
    if (avformat_open_input(&raw, path.toUtf8().constData(), nullptr, nullptr) < 0) return {};
    media::AVFormatInputPtr fmt(raw);
    if (avformat_find_stream_info(fmt.get(), nullptr) < 0) return {};
    const AVCodec* codec = nullptr;
    const int stream = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream < 0 || !codec) return {};
    media::AVCodecContextPtr ctx(avcodec_alloc_context3(codec));
    avcodec_parameters_to_context(ctx.get(), fmt->streams[stream]->codecpar);
    ctx->thread_count = 2;
    if (avcodec_open2(ctx.get(), codec, nullptr) < 0) return {};

    const AVRational tb = fmt->streams[stream]->time_base;
    const std::int64_t target = static_cast<std::int64_t>(seconds / av_q2d(tb));
    if (seconds > 0) av_seek_frame(fmt.get(), stream, target, AVSEEK_FLAG_BACKWARD);

    media::Packet pkt = media::Packet::alloc();
    media::Frame frame = media::Frame::alloc();
    media::Frame best;
    bool reached = false;
    for (int packets = 0; !reached && packets < 600 && av_read_frame(fmt.get(), pkt.get()) >= 0;) {
        if (pkt->stream_index == stream) {
            ++packets;
            avcodec_send_packet(ctx.get(), pkt.get());
            while (!reached && avcodec_receive_frame(ctx.get(), frame.get()) >= 0) {
                best = frame.ref();
                const std::int64_t pts = frame->best_effort_timestamp;
                av_frame_unref(frame.get());
                reached = pts != AV_NOPTS_VALUE && pts >= target;
            }
        }
        av_packet_unref(pkt.get());
    }
    if (!best) return {};
    const int h = std::min(maxHeight, best->height);
    const int w = std::max(2, static_cast<int>(static_cast<double>(best->width) * h / best->height) & ~1);
    media::SwsContextPtr sws(sws_getContext(best->width, best->height, static_cast<AVPixelFormat>(best->format), w, h,
                                            AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!sws) return {};
    QImage image(w, h, QImage::Format_RGBA8888);
    std::uint8_t* dst[4] = {image.bits(), nullptr, nullptr, nullptr};
    int stride[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    sws_scale(sws.get(), best->data, best->linesize, 0, best->height, dst, stride);
    return image;
}

}  // namespace

QImage grabFrame(const QString& path, double seconds, int maxHeight) {
    const QString key = path + QLatin1Char('@') + QString::number(seconds, 'f', 2) + QLatin1Char('#') + QString::number(maxHeight);
    QImage image = cached(key);
    if (image.isNull()) {
        image = decodeFrame(path, seconds, maxHeight);
        if (!image.isNull()) store(key, image);
    }
    return image;
}

}  // namespace lectern::ui
