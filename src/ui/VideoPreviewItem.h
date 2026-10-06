#pragma once

#include "capture/LiveSources.h"

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace lectern::ui {

/// QML handle for a running video source (shared, non-owning for QML).
class PreviewSource : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by controllers")
public:
    explicit PreviewSource(std::shared_ptr<capture::LiveVideoSource> source, QObject* parent = nullptr)
        : QObject(parent), source_(std::move(source)) {}
    [[nodiscard]] std::shared_ptr<capture::LiveVideoSource> source() const { return source_; }

private:
    std::shared_ptr<capture::LiveVideoSource> source_;
};

/// Shows the newest frame of a live source (camera preview while recording
/// setup is open). A worker thread converts at most 30 frames/s, scaled to
/// the item's on-screen size, so the UI and render threads only upload a
/// small texture. (Zero-copy Metal texture import is planned for the editor
/// preview, docs/RENDERING_PIPELINE.md §5.)
class VideoPreviewItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(lectern::ui::PreviewSource* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool mirrored READ mirrored WRITE setMirrored NOTIFY mirroredChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    Q_PROPERTY(bool fill READ fill WRITE setFill NOTIFY fillChanged)

public:
    explicit VideoPreviewItem(QQuickItem* parent = nullptr);
    ~VideoPreviewItem() override;

    [[nodiscard]] PreviewSource* source() const { return source_; }
    void setSource(PreviewSource* source);
    [[nodiscard]] bool mirrored() const { return mirrored_; }
    void setMirrored(bool m);
    [[nodiscard]] bool hasFrame() const { return hasFrame_; }
    [[nodiscard]] bool fill() const { return fill_; }
    void setFill(bool f);

signals:
    void sourceChanged();
    void mirroredChanged();
    void hasFrameChanged();
    void fillChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void workerLoop(std::stop_token stop);
    void onFrameReady();

    QPointer<PreviewSource> source_;
    bool mirrored_ = false;
    bool hasFrame_ = false;
    bool fill_ = false;

    std::mutex mutex_;  // guards live_, pending_, newImage_
    std::shared_ptr<capture::LiveVideoSource> live_;
    QImage pending_;
    bool newImage_ = false;
    QSize imageSize_;
    std::atomic<int> targetWidth_{640};
    std::atomic<int> targetHeight_{360};
    std::jthread worker_;
};

}  // namespace lectern::ui
