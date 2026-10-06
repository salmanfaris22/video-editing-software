#pragma once

#include <QHash>
#include <QMutex>
#include <QQuickImageProvider>

namespace lectern::ui {

/// image://icon/<name>/<RRGGBB>
///
/// Renders a monochrome SVG icon in the requested color and caches the
/// raster. Recoloring at the source avoids a per-icon shader effect pass and
/// works with every scene-graph backend (including the software renderer).
class IconProvider final : public QQuickImageProvider {
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    QMutex mutex_;
    QHash<QString, QImage> cache_;
};

}  // namespace lectern::ui
