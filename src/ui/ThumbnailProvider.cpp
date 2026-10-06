#include "ui/ThumbnailProvider.h"

#include "media/Frame.h"

#include <QRunnable>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>

namespace lectern::ui {

std::mutex ThumbnailProvider::mutex_;
std::list<std::pair<QString, QImage>> ThumbnailProvider::cache_;

QImage ThumbnailProvider::cached(const QString& key) {
    std::lock_guard lock(mutex_);
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->first == key) {
            cache_.splice(cache_.begin(), cache_, it);  // most recently used
            return cache_.front().second;
        }
    }
    return {};
}

void ThumbnailProvider::store(const QString& key, const QImage& image) {
    std::lock_guard lock(mutex_);
    cache_.emplace_front(key, image);
    while (cache_.size() > 64) cache_.pop_back();
}

namespace {

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

class ThumbnailResponse final : public QQuickImageResponse, public QRunnable {
public:
    ThumbnailResponse(QString id, QSize requested) : id_(std::move(id)), requested_(requested) { setAutoDelete(false); }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }

    void run() override {
        const QString path = QUrl::fromPercentEncoding(id_.section(QLatin1Char('?'), 0, 0).toUtf8());
        const QUrlQuery query(id_.section(QLatin1Char('?'), 1));
        const double t = query.queryItemValue(QStringLiteral("t")).toDouble();
        const int height = requested_.height() > 0 ? requested_.height() : 360;
        const QString key = path + QLatin1Char('@') + QString::number(t, 'f', 2) + QLatin1Char('#') + QString::number(height);
        image_ = ThumbnailProvider::cached(key);
        if (image_.isNull()) {
            image_ = decodeFrame(path, t, height);
            if (!image_.isNull()) ThumbnailProvider::store(key, image_);
        }
        emit finished();
    }

private:
    QString id_;
    QSize requested_;
    QImage image_;
};

}  // namespace

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new ThumbnailResponse(id, requestedSize);
    QThreadPool::globalInstance()->start(response);
    return response;
}

}  // namespace lectern::ui
