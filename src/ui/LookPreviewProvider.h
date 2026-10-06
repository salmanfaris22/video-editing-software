#pragma once

#include <QQuickImageProvider>

namespace lectern::ui {

/// image://look/<look id>?r=<revision>
///
/// Serves the look thumbnails from lookpreviews (rendered by
/// ProjectController::refreshLookPreviews). The revision only defeats QML's
/// image cache; a look not rendered yet shows as a dark tile.
class LookPreviewProvider final : public QQuickImageProvider {
public:
    LookPreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

}  // namespace lectern::ui
