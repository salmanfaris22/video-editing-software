#include "ui/IconProvider.h"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

namespace lectern::ui {

QImage IconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const QString name = id.section(QLatin1Char('/'), 0, 0);
    QString color = id.section(QLatin1Char('/'), 1, 1);
    if (color.size() == 8) color = color.right(6);  // drop alpha from #AARRGGBB
    if (color.isEmpty()) color = QStringLiteral("FFFFFF");
    const int edge = requestedSize.isValid() && requestedSize.width() > 0 ? requestedSize.width() : 48;
    const QString key = name + QLatin1Char('/') + color + QLatin1Char('/') + QString::number(edge);

    QMutexLocker lock(&mutex_);
    if (auto it = cache_.find(key); it != cache_.end()) {
        if (size) *size = it->size();
        return *it;
    }
    lock.unlock();

    QFile file(QStringLiteral(":/qt/qml/Lectern/UI/icons/") + name + QStringLiteral(".svg"));
    QImage image(edge, edge, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray svg = file.readAll();
        svg.replace("#FFFFFF", QByteArray("#") + color.toLatin1());
        QSvgRenderer renderer(svg);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
    }
    lock.relock();
    if (cache_.size() > 512) cache_.clear();
    cache_.insert(key, image);
    if (size) *size = image.size();
    return image;
}

}  // namespace lectern::ui
