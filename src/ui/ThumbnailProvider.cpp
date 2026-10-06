#include "ui/ThumbnailProvider.h"

#include "ui/FrameGrab.h"

#include <QRunnable>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>

namespace lectern::ui {

namespace {

class ThumbnailResponse final : public QQuickImageResponse, public QRunnable {
public:
    ThumbnailResponse(QString id, QSize requested) : id_(std::move(id)), requested_(requested) { setAutoDelete(false); }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }

    void run() override {
        const QString path = QUrl::fromPercentEncoding(id_.section(QLatin1Char('?'), 0, 0).toUtf8());
        const QUrlQuery query(id_.section(QLatin1Char('?'), 1));
        const double t = query.queryItemValue(QStringLiteral("t")).toDouble();
        const int height = requested_.height() > 0 ? requested_.height() : 360;
        image_ = grabFrame(path, t, height);
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
