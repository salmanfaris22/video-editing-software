#include "editor/FrameProvider.h"

#include "core/Log.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace lectern::editor {

namespace {

constexpr std::uint64_t kIdleCalls = 1800;  // ~1 min of 30 fps rendering without the media: close it

int coefficientsFor(const AVFrame* f) {
    switch (f->colorspace) {
        case AVCOL_SPC_BT709: return SWS_CS_ITU709;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: return SWS_CS_BT2020;
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_BT470BG: return SWS_CS_ITU601;
        default: return f->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    }
}

/// Size to decode to: enough pixels for the box (after zoom/crop), never more than the source.
QSize targetSize(QSize source, QSizeF box, const VisualLayer& layer) {
    if (source.isEmpty() || box.isEmpty()) return source;
    const double sx = box.width() / source.width();
    const double sy = box.height() / source.height();
    const double scale = std::min(1.0, (layer.fill ? std::max(sx, sy) : std::min(sx, sy)) * layer.zoom);
    return {std::max(2, static_cast<int>(std::lround(source.width() * scale))),
            std::max(2, static_cast<int>(std::lround(source.height() * scale)))};
}

}  // namespace

struct FrameProvider::Entry {
    std::unique_ptr<media::VideoReader> reader;
    media::SwsContextPtr sws;
    QImage still;         ///< image media
    QImage scaledStill;
    QImage last;          ///< last converted video frame
    std::int64_t lastPts = AV_NOPTS_VALUE;
    bool failed = false;
    std::uint64_t lastUse = 0;
};

/// `deep`: 10 bits per channel (QImage::Format_BGR30) for HDR / wide-gamut sources,
/// which the renderers convert into the working space; otherwise 8-bit RGB32.
QImage frameToImage(const AVFrame* frame, QSize size, media::SwsContextPtr& sws, bool deep) {
    if (!frame || frame->width <= 0 || frame->height <= 0 || size.isEmpty()) return {};
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    constexpr AVPixelFormat kRgb32 = AV_PIX_FMT_BGRA;  // Format_RGB32 memory order: B G R A
    constexpr AVPixelFormat kRgb30 = AV_PIX_FMT_X2BGR10LE;  // Format_BGR30: R in the low 10 bits
#else
    constexpr AVPixelFormat kRgb32 = AV_PIX_FMT_ARGB;
    constexpr AVPixelFormat kRgb30 = AV_PIX_FMT_X2BGR10BE;
#endif
    sws.reset(sws_getCachedContext(sws.release(), frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                   size.width(), size.height(), deep ? kRgb30 : kRgb32, SWS_BILINEAR, nullptr, nullptr,
                                   nullptr));
    if (!sws) return {};
    const int srcRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    sws_setColorspaceDetails(sws.get(), sws_getCoefficients(coefficientsFor(frame)), srcRange,
                             sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
    QImage image(size, deep ? QImage::Format_BGR30 : QImage::Format_RGB32);
    if (image.isNull()) return {};
    std::uint8_t* dst[4] = {image.bits(), nullptr, nullptr, nullptr};
    int dstStride[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    sws_scale(sws.get(), frame->data, frame->linesize, 0, frame->height, dst, dstStride);
    return image;
}

FrameProvider::FrameProvider(std::filesystem::path projectDir, bool hardwareDecode)
    : dir_(std::move(projectDir)), hardware_(hardwareDecode) {}

FrameProvider::~FrameProvider() = default;

void FrameProvider::setProject(std::shared_ptr<const project::Project> project) { project_ = std::move(project); }

void FrameProvider::clear() { entries_.clear(); }

QImage FrameProvider::image(const VisualLayer& layer, QSizeF boxPixels) {
    ++calls_;
    std::erase_if(entries_, [this](const auto& e) { return calls_ - e.second->lastUse > kIdleCalls; });
    if (!project_) return {};
    const project::MediaSource* media = project_->findMedia(layer.media);
    if (!media) return {};
    auto& slot = entries_[layer.media];
    if (!slot) slot = std::make_unique<Entry>();
    Entry& e = *slot;
    e.lastUse = calls_;
    if (e.failed) return {};
    const std::filesystem::path path = dir_ / media->path;

    if (media->kind == project::MediaKind::Image) {
        if (e.still.isNull()) {
            QImage loaded(QString::fromStdString(path.string()));
            if (loaded.isNull()) {
                LEC_WARN("editor", "cannot load image '{}'", media->name);
                e.failed = true;
                return {};
            }
            e.still = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        const QSize target = targetSize(e.still.size(), boxPixels, layer);
        if (target == e.still.size()) return e.still;
        if (e.scaledStill.size() != target) {
            e.scaledStill = e.still.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        return e.scaledStill;
    }

    if (!e.reader) {
        auto reader = media::VideoReader::open(path, {.hardwareDecode = hardware_});
        if (!reader) {
            LEC_WARN("editor", "cannot open '{}': {}", media->name, reader.error().message());
            e.failed = true;
            return {};
        }
        e.reader = std::move(*reader);
    }
    auto frame = e.reader->frameAt(layer.sourceTime);
    if (!frame) return e.last;  // keep showing the last good frame on a decode hiccup
    const QSize target = targetSize({(*frame)->width, (*frame)->height}, boxPixels, layer);
    const std::int64_t pts = (*frame)->best_effort_timestamp;
    const bool deep = !layer.input.isIdentity();
    const QImage::Format format = deep ? QImage::Format_BGR30 : QImage::Format_RGB32;
    if (!e.last.isNull() && pts == e.lastPts && pts != AV_NOPTS_VALUE && e.last.size() == target && e.last.format() == format) {
        return e.last;
    }
    QImage image = frameToImage(frame->get(), target, e.sws, deep);
    if (image.isNull()) return e.last;
    e.last = image;
    e.lastPts = pts;
    return image;
}

}  // namespace lectern::editor
