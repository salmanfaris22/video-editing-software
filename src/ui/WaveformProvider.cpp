#include "ui/WaveformProvider.h"

#include "core/Log.h"

#include <QColor>
#include <QPainter>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace lectern::ui {

std::mutex WaveformProvider::mutex_;
std::map<QString, WaveformProvider::Shared> WaveformProvider::cache_;

namespace {

class WaveformResponse final : public QQuickImageResponse, public QRunnable {
public:
    WaveformResponse(QString id, QSize requested) : id_(std::move(id)), requested_(requested) { setAutoDelete(false); }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }

    void run() override {
        const QString path = QUrl::fromPercentEncoding(id_.section(QLatin1Char('?'), 0, 0).toUtf8());
        const QUrlQuery query(id_.section(QLatin1Char('?'), 1));
        const Time from = Time::fromSecondsF(query.queryItemValue(QStringLiteral("from")).toDouble());
        const Time to = Time::fromSecondsF(query.queryItemValue(QStringLiteral("to")).toDouble());
        QColor color(QLatin1Char('#') + query.queryItemValue(QStringLiteral("color")));
        if (!color.isValid()) color = QColor(224, 164, 58);
        const QSize size(std::clamp(requested_.width() > 0 ? requested_.width() : 512, 2, 4096),
                         std::clamp(requested_.height() > 0 ? requested_.height() : 40, 2, 512));
        if (const auto peaks = WaveformProvider::peaks(path)) {
            image_ = WaveformProvider::render(*peaks, from, to, size, color);
        } else {
            image_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
            image_.fill(Qt::transparent);
        }
        emit finished();
    }

private:
    QString id_;
    QSize requested_;
    QImage image_;
};

}  // namespace

std::shared_ptr<const media::WaveformPeaks> WaveformProvider::peaks(const QString& path) {
    Shared future;
    std::promise<std::shared_ptr<const media::WaveformPeaks>> promise;
    bool compute = false;
    {
        std::lock_guard lock(mutex_);
        auto it = cache_.find(path);
        if (it == cache_.end()) {
            future = promise.get_future().share();
            cache_.emplace(path, future);
            compute = true;
        } else {
            future = it->second;
        }
    }
    if (compute) {  // the first request computes; the others wait for it
        auto result = media::computeWaveform(path.toStdString());
        if (!result) LEC_DEBUG("ui", "no waveform for {}: {}", path.toStdString(), result.error().message());
        promise.set_value(result ? std::make_shared<const media::WaveformPeaks>(std::move(*result)) : nullptr);
    }
    return future.get();
}

QImage WaveformProvider::render(const media::WaveformPeaks& peaks, Time from, Time to, QSize size, QColor color) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (to <= from) return image;
    QPainter p(&image);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const double mid = size.height() / 2.0;
    const Time span = to - from;
    for (int x = 0; x < size.width(); ++x) {
        const Time a = from + span.scaled(Rational(x, size.width()));
        const Time b = from + span.scaled(Rational(x + 1, size.width()));
        const double level = peaks.maxIn(a, b) / 255.0;
        const double half = std::max(0.5, level * (mid - 1));
        p.drawRect(QRectF(x, mid - half, 1.0, 2 * half));
    }
    return image;
}

QQuickImageResponse* WaveformProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new WaveformResponse(id, requestedSize);
    QThreadPool::globalInstance()->start(response);
    return response;
}

}  // namespace lectern::ui
