#include "ui/LookPreviewProvider.h"

#include "ui/LookPreviews.h"

namespace lectern::ui {

QImage LookPreviewProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    QImage img = lookpreviews::image(id.section(QLatin1Char('?'), 0, 0));
    if (img.isNull()) {
        img = QImage(16, 9, QImage::Format_RGB32);
        img.fill(QColor(0x16, 0x18, 0x1D));
    }
    if (requestedSize.isValid() && !requestedSize.isEmpty() && img.size() != requestedSize) {
        img = img.scaled(requestedSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    if (size) *size = img.size();
    return img;
}

}  // namespace lectern::ui
