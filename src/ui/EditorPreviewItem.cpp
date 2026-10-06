#include "ui/EditorPreviewItem.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>

#include <algorithm>
#include <cmath>

namespace lectern::ui {

EditorPreviewItem::EditorPreviewItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

void EditorPreviewItem::setPlayback(PlaybackController* playback) {
    if (playback_ == playback) return;
    if (frameConnection_) disconnect(frameConnection_);
    playback_ = playback;
    if (playback_) frameConnection_ = connect(playback_, &PlaybackController::frameReady, this, [this] { update(); });
    reportSize();
    update();
    emit playbackChanged();
}

void EditorPreviewItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    reportSize();
    update();
}

void EditorPreviewItem::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickItem::itemChange(change, value);
    // The Edit and Color pages each have a preview: the visible one sets the render size.
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged || change == ItemVisibleHasChanged) reportSize();
}

void EditorPreviewItem::reportSize() {
    if (!playback_ || width() < 2 || height() < 2 || !isVisible()) return;
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    // Render at the shown resolution (capped: beyond 1920 px the preview gains nothing).
    const double scale = std::min(dpr, 1920.0 / std::max(1.0, width()));
    playback_->setPreviewSize(QSize(static_cast<int>(std::lround(width() * scale)) & ~1,
                                    static_cast<int>(std::lround(height() * scale)) & ~1));
}

QSGNode* EditorPreviewItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    const QImage image = playback_ ? playback_->frame() : QImage();
    if (image.isNull()) {
        delete node;
        if (hasFrame_) {
            hasFrame_ = false;
            QMetaObject::invokeMethod(this, &EditorPreviewItem::hasFrameChanged, Qt::QueuedConnection);
        }
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    node->setTexture(window()->createTextureFromImage(image));
    node->setRect(boundingRect());
    if (!hasFrame_) {
        hasFrame_ = true;
        QMetaObject::invokeMethod(this, &EditorPreviewItem::hasFrameChanged, Qt::QueuedConnection);
    }
    return node;
}

}  // namespace lectern::ui
