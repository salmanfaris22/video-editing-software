#include "editor/Compositor.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>
#include <QTextLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <vector>

namespace lectern::editor {

namespace {

QColor color(const std::string& text, QColor fallback = Qt::white) {
    const QColor c(QString::fromStdString(text));
    return c.isValid() ? c : fallback;
}

/// Font for text drawn on the canvas: the style's family when installed,
/// then common UI families. Hinting is off so glyphs scale proportionally:
/// a line that wraps in the preview wraps the same way in a 4K export.
QFont canvasFont(const std::string& family, double pixelSize, int weight) {
    QFont font;
    QStringList families;
    if (!family.empty()) families << QString::fromStdString(family);
    families << QStringLiteral("Inter") << QStringLiteral("Helvetica Neue") << QStringLiteral("Segoe UI")
             << QStringLiteral("Roboto") << QStringLiteral("Arial") << QStringLiteral("DejaVu Sans");
    font.setFamilies(families);
    font.setPixelSize(std::max(6, static_cast<int>(std::lround(pixelSize))));
    font.setWeight(static_cast<QFont::Weight>(std::clamp(weight, 100, 900)));
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

/// One horizontal + vertical box-blur pass on premultiplied ARGB32.
void boxBlurPass(QImage& img, int radius) {
    const int w = img.width();
    const int h = img.height();
    if (radius < 1 || w < 2 || h < 2) return;
    std::vector<std::uint32_t> line(static_cast<std::size_t>(std::max(w, h)));
    auto blurLine = [radius](std::uint32_t* px, int n, int stride, std::vector<std::uint32_t>& tmp) {
        for (int i = 0; i < n; ++i) tmp[static_cast<std::size_t>(i)] = px[static_cast<std::ptrdiff_t>(i) * stride];
        std::uint32_t sum[4] = {0, 0, 0, 0};
        auto add = [&](std::uint32_t v, int sign) {
            for (int c = 0; c < 4; ++c) sum[c] += static_cast<std::uint32_t>(sign * static_cast<int>((v >> (8 * c)) & 0xFF));
        };
        const int window = 2 * radius + 1;
        for (int i = -radius; i <= radius; ++i) add(tmp[static_cast<std::size_t>(std::clamp(i, 0, n - 1))], 1);
        for (int i = 0; i < n; ++i) {
            std::uint32_t out = 0;
            for (int c = 0; c < 4; ++c) out |= ((sum[c] / static_cast<std::uint32_t>(window)) & 0xFF) << (8 * c);
            px[static_cast<std::ptrdiff_t>(i) * stride] = out;
            add(tmp[static_cast<std::size_t>(std::clamp(i - radius, 0, n - 1))], -1);
            add(tmp[static_cast<std::size_t>(std::clamp(i + radius + 1, 0, n - 1))], 1);
        }
    };
    for (int y = 0; y < h; ++y) blurLine(reinterpret_cast<std::uint32_t*>(img.scanLine(y)), w, 1, line);
    auto* base = reinterpret_cast<std::uint32_t*>(img.bits());
    const int stride = static_cast<int>(img.bytesPerLine() / 4);
    for (int x = 0; x < w; ++x) blurLine(base + x, h, stride, line);
}

std::mutex& segmenterMutex() {
    static std::mutex m;
    return m;
}

PersonSegmenter& segmenterSlot() {
    static PersonSegmenter s;
    return s;
}

PersonSegmenter currentSegmenter() {
    std::lock_guard lock(segmenterMutex());
    return segmenterSlot();
}

/// Draws the (possibly partial, for the typewriter) lines of a text block.
void drawTextLines(QPainter& p, const TextBlock& t, const VisualLayer& l, double H) {
    const timeline::TextStyle& s = l.textStyle;
    const double scale = H / 1080.0;
    const QFontMetricsF fm(t.font, p.device());
    if (!s.background.empty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(color(s.background, QColor(0, 0, 0, 160)));
        p.drawRoundedRect(t.block, fm.height() * 0.22, fm.height() * 0.22);
    }
    const Qt::Alignment align = s.alignment == "left"    ? Qt::AlignLeft
                                : s.alignment == "right" ? Qt::AlignRight
                                                         : Qt::AlignHCenter;
    const double alignFactor = s.alignment == "left" ? 0.0 : s.alignment == "right" ? 1.0 : 0.5;
    const int flags = static_cast<int>(align | Qt::AlignVCenter) | Qt::TextDontClip;
    const int partialFlags = static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter) | Qt::TextDontClip;
    p.setFont(t.font);
    int remaining = l.textChars;  // code points still to show (−1 = all)
    for (int i = 0; i < t.lines.size(); ++i) {
        if (remaining == 0) break;
        const QRectF lineRect(t.block.x() + t.padX, t.block.y() + t.padY + i * t.lineHeight, t.textWidth, t.lineHeight);
        QString text = t.lines[i];
        QRectF rect = lineRect;
        int lineFlags = flags;
        if (remaining > 0) {
            const QList<uint> ucs = text.toUcs4();
            if (remaining < ucs.size()) {
                // Typing: keep the letters where the whole line will stand.
                const double full = fm.horizontalAdvance(text);
                rect.setLeft(lineRect.left() + (t.textWidth - full) * alignFactor);
                text = QString::fromUcs4(reinterpret_cast<const char32_t*>(ucs.constData()), remaining);
                lineFlags = partialFlags;
                remaining = 0;
            } else {
                remaining -= static_cast<int>(ucs.size());
            }
        }
        if (s.shadowBlur > 0 && !s.shadowColor.empty()) {
            p.setPen(color(s.shadowColor, QColor(0, 0, 0, 150)));
            p.drawText(rect.translated(0, std::max(1.0, 3 * scale)), lineFlags, text);
        }
        p.setPen(color(s.color));
        p.drawText(rect, lineFlags, text);
    }
}

}  // namespace

void setPersonSegmenter(PersonSegmenter segmenter) {
    std::lock_guard lock(segmenterMutex());
    segmenterSlot() = std::move(segmenter);
}

bool hasPersonSegmenter() {
    std::lock_guard lock(segmenterMutex());
    return static_cast<bool>(segmenterSlot());
}

TextBlock layoutText(const VisualLayer& l, double W, double H, QPaintDevice* device) {
    const timeline::TextStyle& s = l.textStyle;
    const double scale = H / 1080.0;
    TextBlock t;
    t.font = canvasFont(s.font, s.size * scale, s.weight);
    if (s.letterSpacing != 0) t.font.setLetterSpacing(QFont::AbsoluteSpacing, s.letterSpacing * scale);
    const QFontMetricsF fm(t.font, device);  // the device we paint on, not the screen
    t.lines = QString::fromStdString(l.text).split(QLatin1Char('\n'));
    for (const QString& line : t.lines) t.textWidth = std::max(t.textWidth, fm.horizontalAdvance(line));
    t.lineHeight = fm.height() * std::max(0.8, s.lineSpacing) / 1.2 * 1.1;
    const bool boxed = !s.background.empty();
    t.padX = boxed ? fm.height() * 0.45 : 0;
    t.padY = boxed ? fm.height() * 0.22 : 0;
    t.block = QRectF(0, 0, t.textWidth + 2 * t.padX, t.lineHeight * t.lines.size() + 2 * t.padY);
    t.block.moveCenter(QPointF(l.anchorX * W, l.anchorY * H));
    return t;
}

QRectF animatedTextRect(const VisualLayer& l, const TextBlock& t, double W, double H) {
    const double scale = std::max(0.01, l.textScale);
    QRectF r(0, 0, t.block.width() * scale, t.block.height() * scale);
    r.moveCenter(t.block.center() + QPointF(l.textDx * W, l.textDy * H));
    return r;
}

QRectF subtitleRect(const VisualLayer& l, const project::StyleSettings& style, double W, double H,
                    const std::vector<QRectF>& avoid, QPaintDevice* device) {
    const QFont font = canvasFont({}, style.subtitleSize * H, 600);
    QString text = QString::fromStdString(l.text);
    text.replace(QLatin1Char('\n'), QChar::LineSeparator);
    QTextLayout layout(text, font, device);
    QTextOption option(Qt::AlignHCenter);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    const double maxWidth = W * 0.84;
    double height = 0;
    double widest = 0;
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(maxWidth);
        line.setPosition(QPointF(0, height));
        height += line.height();
        widest = std::max(widest, line.naturalTextWidth());
    }
    layout.endLayout();
    const QFontMetricsF fm(font, device);
    const double padX = fm.height() * 0.4;
    const double padY = fm.height() * 0.15;
    QRectF block(0, 0, widest + 2 * padX, height + 2 * padY);
    block.moveCenter(QPointF(l.anchorX * W, l.anchorY * H));
    if (block.bottom() > H - 2) block.moveBottom(H - 2);  // long subtitles stay on the canvas
    // Like broadcast captions, step above a title that is on screen at the same place.
    for (bool moved = true; moved;) {
        moved = false;
        for (const QRectF& title : avoid) {
            if (title.isEmpty() || !title.intersects(block)) continue;
            const double top = title.top() - block.height() - fm.height() * 0.25;
            if (top < 2) continue;  // no room above: keep the requested place
            block.moveTop(top);
            moved = true;
        }
    }
    if (block.top() < 2) block.moveTop(2);
    return block;
}

void blurImage(QImage& img, double radius) {
    if (radius < 0.5 || img.isNull()) return;
    // Large blurs run on a downscaled copy: same look, a fraction of the work.
    const double factor = std::max(1.0, radius / 6.0);
    QImage work = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (factor > 1.0) {
        work = work.scaled(std::max(1, static_cast<int>(img.width() / factor)), std::max(1, static_cast<int>(img.height() / factor)),
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    const int r = std::max(1, static_cast<int>(std::lround(radius / factor / 1.7)));
    for (int pass = 0; pass < 3; ++pass) boxBlurPass(work, r);  // three box passes ≈ Gaussian
    if (factor > 1.0) work = work.scaled(img.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    img = work.convertToFormat(img.format());
}

double glowRadiusPixels(double radius, double height) { return (0.004 + std::clamp(radius, 0.0, 1.0) * 0.05) * height; }
double halationRadiusPixels(double radius, double height) { return (0.002 + std::clamp(radius, 0.0, 1.0) * 0.025) * height; }
double grainSizePixels(double size, double height) { return std::max(0.75, std::clamp(size, 0.5, 4.0) * height / 1080.0 * 1.5); }

void addGlow(QImage& img, double amount, double threshold, double radius, const std::array<double, 3>& tint) {
    if (amount <= 0 || img.isNull()) return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32_Premultiplied) {
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    const double t0 = std::clamp(threshold, 0.0, 0.98);
    const double t1 = std::min(1.0, t0 + 0.25);
    QImage bright(img.size(), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y) {
        const auto* in = reinterpret_cast<const std::uint32_t*>(img.constScanLine(y));
        auto* out = reinterpret_cast<std::uint32_t*>(bright.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const std::uint32_t p = in[x];
            const double r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF, a = p >> 24;
            const double l = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
            const double e = std::clamp((l - t0) / (t1 - t0), 0.0, 1.0);
            const double w = e * e * (3.0 - 2.0 * e);  // smoothstep, as in the shader
            auto c = [w](double v) { return static_cast<std::uint32_t>(std::lround(v * w)); };
            out[x] = (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
        }
    }
    blurImage(bright, radius);
    for (int y = 0; y < img.height(); ++y) {
        auto* px = reinterpret_cast<std::uint32_t*>(img.scanLine(y));
        const auto* add = reinterpret_cast<const std::uint32_t*>(bright.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const std::uint32_t p = px[x];
            const std::uint32_t q = add[x];
            auto mix = [&](int shift, double k) {
                const double v = ((p >> shift) & 0xFF) + ((q >> shift) & 0xFF) * k * amount;
                return static_cast<std::uint32_t>(std::clamp(std::lround(v), 0L, 255L)) << shift;
            };
            px[x] = (p & 0xFF000000u) | mix(16, tint[0]) | mix(8, tint[1]) | mix(0, tint[2]);
        }
    }
}

namespace {
std::uint32_t hash32(std::uint32_t x) {  // lowbias32; the shader has the same function
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
double lattice(std::int64_t ix, std::int64_t iy, std::uint32_t seed) {
    const std::uint32_t h = hash32(static_cast<std::uint32_t>(ix) * 0x8da6b343U ^ hash32(static_cast<std::uint32_t>(iy) * 0xd8163841U ^ seed));
    return static_cast<double>(h & 0xFFFFFFU) / 16777215.0;
}
}  // namespace

double grainNoise(double x, double y, double size, std::uint32_t seed) {
    const double fx = x / size;
    const double fy = y / size;
    const double ix = std::floor(fx);
    const double iy = std::floor(fy);
    const double tx = fx - ix;
    const double ty = fy - iy;
    const auto i = static_cast<std::int64_t>(ix);
    const auto j = static_cast<std::int64_t>(iy);
    const double top = lattice(i, j, seed) + (lattice(i + 1, j, seed) - lattice(i, j, seed)) * tx;
    const double bottom = lattice(i, j + 1, seed) + (lattice(i + 1, j + 1, seed) - lattice(i, j + 1, seed)) * tx;
    return std::clamp((top + (bottom - top) * ty - 0.5) * 3.4, -1.0, 1.0);
}

void addGrain(QImage& img, double amount, double size, std::uint32_t seed) {
    if (amount <= 0 || img.isNull()) return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32_Premultiplied) {
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    for (int y = 0; y < img.height(); ++y) {
        auto* px = reinterpret_cast<std::uint32_t*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const std::uint32_t p = px[x];
            const std::uint32_t a = p >> 24;
            if (a == 0) continue;
            const double r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            const double l = (0.2126 * r + 0.7152 * g + 0.0722 * b) / std::max(1.0, static_cast<double>(a));
            const double w = 0.4 + 2.4 * l * (1.0 - l);  // strongest in the midtones
            const double d = grainNoise(x + 0.5, y + 0.5, size, seed) * amount * 0.12 * w * a;
            auto c = [d](double v) { return static_cast<std::uint32_t>(std::clamp(std::lround(v + d), 0L, 255L)); };
            px[x] = (a << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
        }
    }
}

QPainterPath layerShape(const QRectF& rect, const VisualLayer& layer, double canvasHeight) {
    QPainterPath path;
    if (layer.circle) {
        path.addEllipse(rect);
    } else if (layer.radius > 0) {
        const double r = std::min(layer.radius * canvasHeight, std::min(rect.width(), rect.height()) / 2);
        path.addRoundedRect(rect, r, r);
    } else {
        path.addRect(rect);
    }
    return path;
}

void Compositor::render(const RenderPlan& plan, QImage& target, const ImageSource& images) {
    if (target.isNull()) return;
    const double W = target.width();
    const double H = target.height();
    // The background rarely changes: build it once, blit it every frame.
    const QString key = QString::fromStdString(plan.background + '|' + plan.background2);
    if (key != backgroundKey_ || background_.size() != target.size()) {
        background_ = QImage(target.size(), QImage::Format_RGB32);
        QPainter bp(&background_);
        if (plan.background2.empty()) {
            bp.fillRect(background_.rect(), color(plan.background, QColor(14, 15, 19)));
        } else {
            QLinearGradient g(0, 0, W, H);
            g.setColorAt(0, color(plan.background, QColor(14, 15, 19)));
            g.setColorAt(1, color(plan.background2, QColor(14, 15, 19)));
            bp.fillRect(background_.rect(), g);
        }
        backgroundKey_ = key;
    }
    QPainter p(&target);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(0, 0, background_);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    std::vector<QRectF> titles;  // where text was drawn this frame
    for (const VisualLayer& layer : plan.layers) {
        switch (layer.kind) {
            case LayerKind::Media: drawMedia(p, layer, W, H, images); break;
            case LayerKind::Text: titles.push_back(drawText(p, layer, W, H)); break;
            case LayerKind::Subtitle: drawSubtitle(p, layer, plan.style, W, H, titles); break;
        }
    }
}

void Compositor::drawShadow(QPainter& p, const QRectF& rect, const VisualLayer& layer, double H, bool opaque) {
    // A blurred silhouette, built at quarter resolution, scaled to full size
    // once and cached per shape; drawing it is then a plain blend.
    constexpr double kScale = 0.25;
    const double blur = H * 0.025;
    const int pad = static_cast<int>(std::ceil(blur * 2.5)) + 2;  // full-resolution margin
    const int radius = static_cast<int>(std::lround(layer.radius * H));
    const auto key = std::make_tuple(static_cast<int>(std::lround(rect.width())), static_cast<int>(std::lround(rect.height())),
                                     layer.circle ? -1 : radius, layer.circle);
    auto it = shadowCache_.find(key);
    if (it == shadowCache_.end()) {
        if (shadowCache_.size() > 12) shadowCache_.clear();
        const int w = std::get<0>(key);
        const int h = std::get<1>(key);
        const int smallPad = std::max(2, static_cast<int>(std::lround(pad * kScale)));
        QImage small(std::max(1, static_cast<int>(w * kScale)) + 2 * smallPad,
                     std::max(1, static_cast<int>(h * kScale)) + 2 * smallPad, QImage::Format_ARGB32_Premultiplied);
        small.fill(Qt::transparent);
        QPainter sp(&small);
        sp.setRenderHint(QPainter::Antialiasing);
        sp.setPen(Qt::NoPen);
        sp.setBrush(QColor(0, 0, 0, 255));
        sp.drawPath(layerShape(QRectF(smallPad, smallPad, small.width() - 2 * smallPad, small.height() - 2 * smallPad),
                               layer, H * kScale));
        sp.end();
        blurImage(small, blur * kScale);
        QImage full = small.scaled(w + 2 * pad, h + 2 * pad, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        it = shadowCache_.emplace(key, std::move(full)).first;
    }
    const QImage& shadow = it->second;
    const QPoint at(static_cast<int>(std::lround(rect.x())) - pad,
                    static_cast<int>(std::lround(rect.y() + H * 0.012)) - pad);
    p.save();
    p.setOpacity(std::clamp(layer.shadow, 0.0, 1.0) * 0.75 * layer.opacity);
    if (opaque) {
        // Under an opaque layer only the rim of the shadow can be seen.
        const int inset = layer.circle ? static_cast<int>(std::min(rect.width(), rect.height()) * 0.15) : radius;
        const QRect covered = rect.toAlignedRect().adjusted(inset + 1, inset + 1, -inset - 1, -inset - 1);
        QRegion visible(QRect(at, shadow.size()));
        if (covered.isValid()) visible -= QRegion(covered);
        p.setClipRegion(visible);
    }
    p.drawImage(at, shadow);
    p.restore();
}

void Compositor::drawMedia(QPainter& p, const VisualLayer& l, double W, double H, const ImageSource& images) {
    const QRectF box(l.box.x * W, l.box.y * H, l.box.w * W, l.box.h * H);
    if (box.width() < 1 || box.height() < 1 || l.opacity <= 0) return;
    const QImage src = images ? images(l, box.size()) : QImage();
    if (src.isNull()) {  // missing media: keep the layout readable
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(30, 33, 40));
        p.setOpacity(l.opacity);
        p.drawPath(layerShape(box, l, H));
        p.restore();
        return;
    }

    QRectF srcRect(0, 0, src.width(), src.height());
    if (l.cropL > 0 || l.cropT > 0 || l.cropR > 0 || l.cropB > 0) {
        const double w = srcRect.width();
        const double h = srcRect.height();
        srcRect = QRectF(srcRect.x() + w * l.cropL, srcRect.y() + h * l.cropT, w * (1.0 - l.cropL - l.cropR),
                         h * (1.0 - l.cropT - l.cropB));
    }
    if (l.zoom > 1.0001) {
        const double w = srcRect.width() / l.zoom;
        const double h = srcRect.height() / l.zoom;
        const double cx = std::clamp(l.zoomX * src.width(), w / 2, src.width() - w / 2);
        const double cy = std::clamp(l.zoomY * src.height(), h / 2, src.height() - h / 2);
        srcRect = QRectF(cx - w / 2, cy - h / 2, w, h);
    }
    QRectF dest = box;
    if (l.fill) {  // crop the source to the box's aspect
        const double boxAspect = box.width() / box.height();
        const double srcAspect = srcRect.width() / srcRect.height();
        if (srcAspect > boxAspect) {
            const double w = srcRect.height() * boxAspect;
            srcRect.setX(srcRect.x() + (srcRect.width() - w) / 2);
            srcRect.setWidth(w);
        } else {
            const double h = srcRect.width() / boxAspect;
            srcRect.setY(srcRect.y() + (srcRect.height() - h) / 2);
            srcRect.setHeight(h);
        }
    } else {  // fit inside the box
        const double scale = std::min(box.width() / srcRect.width(), box.height() / srcRect.height());
        dest = QRectF(0, 0, srcRect.width() * scale, srcRect.height() * scale);
        dest.moveCenter(box.center());
    }

    QSize size(std::max(1, static_cast<int>(std::lround(dest.width()))),
               std::max(1, static_cast<int>(std::lround(dest.height()))));
    const QRect srcPixels = srcRect.toAlignedRect().intersected(src.rect());
    QImage layer = srcPixels == src.rect() ? src : src.copy(srcPixels);
    // The frame provider already decodes to about the shown size: within a
    // couple of pixels, draw 1:1 instead of resampling the whole picture.
    if (std::abs(layer.width() - size.width()) <= 2 && std::abs(layer.height() - size.height()) <= 2) {
        size = layer.size();
    } else {
        layer = layer.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (l.mirror) layer = layer.flipped(Qt::Horizontal);
    applyInputColor(layer, l.input);  // into the working space (Rec.709); 8-bit from here on
    if (!l.color.isIdentity()) {
        const std::shared_ptr<const Lut3D> lut = l.color.lut.empty() ? nullptr : luts_.get(l.color.lut, projectDir_);
        applyColor(layer, l.color, lut.get());
    }
    if (!l.nodes.empty()) {
        // Windows are placed in source coordinates: map the layer's pixels back.
        SourceMap map;
        map.u0 = srcPixels.x() / static_cast<double>(src.width());
        map.du = srcPixels.width() / (static_cast<double>(src.width()) * size.width());
        map.v0 = srcPixels.y() / static_cast<double>(src.height());
        map.dv = srcPixels.height() / (static_cast<double>(src.height()) * size.height());
        map.mirror = l.mirror;
        map.aspect = src.width() / static_cast<double>(src.height());
        const bool subjects = std::any_of(l.nodes.begin(), l.nodes.end(), [](const NodeParams& n) { return n.subject != 0; });
        const QImage mask = subjects ? personMask(layer, l) : QImage();
        applyNodes(layer, l.nodes, map, mask.isNull() ? nullptr : &mask, l.highlightNode);
    }
    if (l.glow > 0) addGlow(layer, l.glow, l.glowThreshold, glowRadiusPixels(l.glowRadius, H), kGlowTint);
    if (l.halation > 0) addGlow(layer, l.halation, l.halationThreshold, halationRadiusPixels(l.halationRadius, H), kHalationTint);
    if (l.backgroundBlur > 0) blurBackground(layer, l, H);
    if (l.blur > 0) blurImage(layer, l.blur * 0.03 * H);
    if (l.grain > 0) addGrain(layer, l.grain, grainSizePixels(l.grainSize, H), l.grainSeed);
    // Whole-pixel placement keeps blits exact (no resampling on draw).
    const QRect destPixels(QPoint(static_cast<int>(std::lround(dest.center().x() - size.width() / 2.0)),
                                  static_cast<int>(std::lround(dest.center().y() - size.height() / 2.0))),
                           size);
    dest = QRectF(destPixels);
    const bool opaque = !layer.hasAlphaChannel() && l.opacity >= 0.999;

    if (l.shadow > 0) drawShadow(p, dest, l, H, opaque);
    const QPainterPath shape = layerShape(dest, l, H);
    p.save();
    p.setOpacity(l.opacity);
    if (std::abs(l.rotation) > 0.01) {
        p.translate(dest.center());
        p.rotate(l.rotation);
        p.translate(-dest.center());
    }
    if (!l.circle && l.radius <= 0) {
        p.drawImage(destPixels.topLeft(), layer);
    } else if (!l.circle) {
        // Rounded rectangle: blit everything but the corner squares, and
        // fill only those with the antialiased texture-brush path.
        const int r = std::min(static_cast<int>(std::ceil(l.radius * H)), std::min(size.width(), size.height()) / 2);
        const QRect& d = destPixels;
        QRegion corners;
        corners += QRect(d.left(), d.top(), r, r);
        corners += QRect(d.right() - r + 1, d.top(), r, r);
        corners += QRect(d.left(), d.bottom() - r + 1, r, r);
        corners += QRect(d.right() - r + 1, d.bottom() - r + 1, r, r);
        p.setClipRegion(QRegion(d) - corners);
        p.drawImage(d.topLeft(), layer);
        p.setClipRegion(corners);
        QBrush brush(layer);
        brush.setTransform(QTransform::fromTranslate(d.x(), d.y()));
        p.setPen(Qt::NoPen);
        p.setBrush(brush);
        p.drawPath(shape);
        p.setClipping(false);
    } else {
        // Circle: small, so the antialiased texture-brush path is fine.
        QBrush brush(layer);
        brush.setTransform(QTransform::fromTranslate(dest.x(), dest.y()));
        p.setPen(Qt::NoPen);
        p.setBrush(brush);
        p.drawPath(shape);
    }
    if (l.vignette > 0) {
        QRadialGradient g(dest.center(), std::hypot(dest.width(), dest.height()) / 2);
        g.setColorAt(0.5, QColor(0, 0, 0, 0));
        g.setColorAt(1.0, QColor(0, 0, 0, static_cast<int>(230 * std::clamp(l.vignette, 0.0, 1.0))));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(shape);
    }
    if (l.border > 0) {
        QPen pen(color(l.borderColor));
        pen.setWidthF(std::max(1.0, l.border * H));
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape);
    }
    p.restore();
}

QRectF Compositor::drawText(QPainter& p, const VisualLayer& l, double W, double H) {
    if (l.opacity <= 0.001 || l.textChars == 0 || l.textReveal <= 0) return {};
    const TextBlock t = layoutText(l, W, H, p.device());
    const QRectF shown = animatedTextRect(l, t, W, H);
    p.save();
    p.setOpacity(l.opacity);
    // Animation: move, then scale about the block's center.
    const QPointF center = t.block.center();
    p.translate(l.textDx * W, l.textDy * H);
    if (std::abs(l.textScale - 1.0) > 1e-4) {
        p.translate(center);
        p.scale(l.textScale, l.textScale);
        p.translate(-center);
    }
    if (l.textReveal < 1.0) {  // wipe: show the left part
        const QRectF visible(t.block.x() - t.block.height(), t.block.y() - t.block.height(),
                             t.block.height() + t.block.width() * l.textReveal, t.block.height() * 3);
        p.setClipRect(visible, Qt::IntersectClip);
    }
    const double blur = l.textBlur * H;
    if (blur >= 0.5) {
        // Blur-in: draw the block into its own image and soften it.
        const int pad = static_cast<int>(std::ceil(blur * 3));
        const QRect area = t.block.toAlignedRect().adjusted(-pad, -pad, pad, pad);
        QImage img(area.size(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter ip(&img);
            ip.setRenderHints(p.renderHints());
            ip.translate(-area.topLeft());
            drawTextLines(ip, t, l, H);
        }
        blurImage(img, blur);
        p.drawImage(area.topLeft(), img);
    } else {
        drawTextLines(p, t, l, H);
    }
    p.restore();
    return l.opacity > 0.01 ? shown : QRectF();
}

void Compositor::drawSubtitle(QPainter& p, const VisualLayer& l, const project::StyleSettings& style, double W,
                              double H, const std::vector<QRectF>& avoid) {
    const QRectF block = subtitleRect(l, style, W, H, avoid, p.device());
    const QFont font = canvasFont({}, style.subtitleSize * H, 600);
    QString text = QString::fromStdString(l.text);
    text.replace(QLatin1Char('\n'), QChar::LineSeparator);
    // The same layout as subtitleRect: the box and the lines always agree.
    QTextLayout layout(text, font, p.device());
    QTextOption option(Qt::AlignHCenter);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    const double maxWidth = W * 0.84;
    double height = 0;
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(maxWidth);
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    layout.endLayout();
    const QFontMetricsF fm(font, p.device());
    const double padY = fm.height() * 0.15;
    p.save();
    if (!style.subtitleBackground.empty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(color(style.subtitleBackground, QColor(0, 0, 0, 180)));
        p.drawRoundedRect(block, fm.height() * 0.2, fm.height() * 0.2);
    }
    p.setPen(color(style.subtitleColor));
    // Lines are centered within maxWidth; center that column on the block.
    layout.draw(&p, QPointF(block.center().x() - maxWidth / 2, block.top() + padY));
    p.restore();
}

QImage Compositor::segmentPerson(const QImage& image) {
    const PersonSegmenter segmenter = currentSegmenter();
    if (!segmenter || image.isNull()) return {};
    QImage mask = segmenter(image.convertToFormat(QImage::Format_RGB32));
    if (mask.isNull()) return {};
    return mask.convertToFormat(QImage::Format_Grayscale8).scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage Compositor::personMask(const QImage& img, const VisualLayer& l) {
    const PersonSegmenter segmenter = currentSegmenter();
    if (!segmenter || img.isNull()) return {};
    const std::string key = l.media.toString() + '@' + std::to_string(l.sourceTime.ticks()) + '#' +
                            std::to_string(img.width()) + 'x' + std::to_string(img.height()) + (l.mirror ? "m" : "");
    for (const MaskEntry& e : masks_) {
        if (e.key == key) return e.mask;
    }
    QImage mask = segmenter(img.convertToFormat(QImage::Format_RGB32));
    if (mask.isNull()) return {};
    mask = mask.convertToFormat(QImage::Format_Grayscale8).scaled(img.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (masks_.size() >= 3) masks_.erase(masks_.begin());
    masks_.push_back({key, mask});
    return mask;
}

void Compositor::blurBackground(QImage& img, const VisualLayer& l, double H) {
    (void)H;
    const QImage mask = personMask(img, l);
    if (mask.isNull()) return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32_Premultiplied) {
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QImage blurred = img;
    blurImage(blurred, std::clamp(l.backgroundBlur, 0.0, 1.0) * 0.045 * img.height());
    // out = person · sharp + (1 − person) · blurred
    for (int y = 0; y < img.height(); ++y) {
        auto* out = reinterpret_cast<std::uint32_t*>(img.scanLine(y));
        const auto* soft = reinterpret_cast<const std::uint32_t*>(blurred.constScanLine(y));
        const std::uint8_t* m = mask.constScanLine(y);
        for (int x = 0; x < img.width(); ++x) {
            const std::uint32_t a = m[x];
            if (a == 255) continue;
            const std::uint32_t s = out[x];
            const std::uint32_t b = soft[x];
            std::uint32_t mixed = 0;
            for (int c = 0; c < 32; c += 8) {
                const std::uint32_t sv = (s >> c) & 0xFF;
                const std::uint32_t bv = (b >> c) & 0xFF;
                mixed |= ((sv * a + bv * (255 - a) + 127) / 255) << c;
            }
            out[x] = mixed;
        }
    }
}

}  // namespace lectern::editor
