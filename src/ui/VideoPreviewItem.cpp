#include "ui/VideoPreviewItem.h"

#include "core/Thread.h"
#include "media/Frame.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>

#include <algorithm>
#include <chrono>

namespace lectern::ui {

namespace {

/// Converts a (possibly hardware) frame to an RGBA image of at most w×h,
/// keeping aspect ratio. Maps GPU frames without copying when possible.
QImage toImage(const AVFrame* src, int maxW, int maxH, media::SwsContextPtr& sws) {
    media::Frame mapped;
    const AVFrame* in = src;
    if (media::isHardwarePixelFormat(static_cast<AVPixelFormat>(src->format))) {
        mapped = media::Frame::alloc();
        if (src->hw_frames_ctx) {
            mapped->format = reinterpret_cast<const AVHWFramesContext*>(src->hw_frames_ctx->data)->sw_format;
        }
        if (av_hwframe_map(mapped.get(), src, AV_HWFRAME_MAP_READ) < 0) {
            mapped = media::Frame::alloc();
            if (av_hwframe_transfer_data(mapped.get(), src, 0) < 0) return {};
        }
        in = mapped.get();
    }
    if (in->width <= 0 || in->height <= 0) return {};
    const double scale = std::min({1.0, static_cast<double>(maxW) / in->width, static_cast<double>(maxH) / in->height});
    const int w = std::max(2, static_cast<int>(in->width * scale) & ~1);
    const int h = std::max(2, static_cast<int>(in->height * scale) & ~1);
    sws.reset(sws_getCachedContext(sws.release(), in->width, in->height, static_cast<AVPixelFormat>(in->format), w, h,
                                   AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!sws) return {};
    QImage image(w, h, QImage::Format_RGBA8888);
    std::uint8_t* dst[4] = {image.bits(), nullptr, nullptr, nullptr};
    int dstStride[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    sws_scale(sws.get(), in->data, in->linesize, 0, in->height, dst, dstStride);
    return image;
}

}  // namespace

VideoPreviewItem::VideoPreviewItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    worker_ = std::jthread([this](std::stop_token st) { workerLoop(st); });
}

VideoPreviewItem::~VideoPreviewItem() {
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
}

void VideoPreviewItem::setSource(PreviewSource* source) {
    if (source_ == source) return;
    source_ = source;
    {
        std::lock_guard lock(mutex_);
        live_ = source ? source->source() : nullptr;
        pending_ = {};
        newImage_ = false;
    }
    if (hasFrame_) {
        hasFrame_ = false;
        emit hasFrameChanged();
    }
    update();
    emit sourceChanged();
}

void VideoPreviewItem::setMirrored(bool m) {
    if (mirrored_ == m) return;
    mirrored_ = m;
    update();
    emit mirroredChanged();
}

void VideoPreviewItem::setFill(bool f) {
    if (fill_ == f) return;
    fill_ = f;
    update();
    emit fillChanged();
}

void VideoPreviewItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 2.0;
    targetWidth_ = std::clamp(static_cast<int>(newGeometry.width() * dpr), 64, 1280);
    targetHeight_ = std::clamp(static_cast<int>(newGeometry.height() * dpr), 64, 720);
    update();
}

void VideoPreviewItem::workerLoop(std::stop_token stop) {
    setCurrentThreadName("lectern.ui.preview");
    media::SwsContextPtr sws;
    std::uint64_t lastSequence = UINT64_MAX;
    while (!stop.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
        std::shared_ptr<capture::LiveVideoSource> live;
        {
            std::lock_guard lock(mutex_);
            live = live_;
        }
        if (!live) continue;
        auto frame = live->latestFrame();
        if (!frame || !frame->frame || frame->sequence == lastSequence) continue;
        lastSequence = frame->sequence;
        QImage image = toImage(frame->frame.get(), targetWidth_.load(), targetHeight_.load(), sws);
        if (image.isNull()) continue;
        {
            std::lock_guard lock(mutex_);
            if (live_ != live) continue;  // source changed meanwhile
            pending_ = std::move(image);
            newImage_ = true;
        }
        QMetaObject::invokeMethod(this, &VideoPreviewItem::onFrameReady, Qt::QueuedConnection);
    }
}

void VideoPreviewItem::onFrameReady() {
    if (!hasFrame_) {
        hasFrame_ = true;
        emit hasFrameChanged();
    }
    update();
}

QSGNode* VideoPreviewItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    {
        std::lock_guard lock(mutex_);
        if (!live_) {
            delete node;
            return nullptr;
        }
        if (newImage_) {
            if (!node) {
                node = new QSGSimpleTextureNode();
                node->setOwnsTexture(true);
                node->setFiltering(QSGTexture::Linear);
            }
            node->setTexture(window()->createTextureFromImage(pending_));
            imageSize_ = pending_.size();
            newImage_ = false;
        }
    }
    if (!node || !node->texture()) {
        delete node;
        return nullptr;
    }
    // Aspect-fit (or fill) inside the item.
    const QSizeF img = imageSize_;
    const QRectF bounds = boundingRect();
    const qreal sx = bounds.width() / img.width();
    const qreal sy = bounds.height() / img.height();
    const qreal s = fill_ ? std::max(sx, sy) : std::min(sx, sy);
    const QSizeF size(img.width() * s, img.height() * s);
    QRectF rect(bounds.center() - QPointF(size.width() / 2, size.height() / 2), size);
    if (fill_) {
        // Crop through texture coordinates instead of drawing outside the item.
        const QRectF visible = rect.intersected(bounds);
        node->setSourceRect(QRectF((visible.left() - rect.left()) / s, (visible.top() - rect.top()) / s,
                                   visible.width() / s, visible.height() / s));
        rect = visible;
    } else {
        node->setSourceRect(QRectF(QPointF(0, 0), img));
    }
    node->setRect(rect);
    node->setTextureCoordinatesTransform(mirrored_ ? QSGSimpleTextureNode::MirrorHorizontally
                                                   : QSGSimpleTextureNode::NoTransform);
    return node;
}

}  // namespace lectern::ui
