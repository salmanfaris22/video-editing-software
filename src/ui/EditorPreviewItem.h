#pragma once

#include "ui/PlaybackController.h"

#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace lectern::ui {

/// Shows the editor's composited canvas (rendered by the playback engine at
/// this item's pixel size) as a scene-graph texture.
class EditorPreviewItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(lectern::ui::PlaybackController* playback READ playback WRITE setPlayback NOTIFY playbackChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)

public:
    explicit EditorPreviewItem(QQuickItem* parent = nullptr);

    [[nodiscard]] PlaybackController* playback() const { return playback_; }
    void setPlayback(PlaybackController* playback);
    [[nodiscard]] bool hasFrame() const { return hasFrame_; }

signals:
    void playbackChanged();
    void hasFrameChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void reportSize();

    QPointer<PlaybackController> playback_;
    QMetaObject::Connection frameConnection_;
    bool hasFrame_ = false;
};

}  // namespace lectern::ui
