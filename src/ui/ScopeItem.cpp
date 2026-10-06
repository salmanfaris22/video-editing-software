#include "ui/ScopeItem.h"

#include "ui/PlaybackController.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace lectern::ui {

namespace {
const QColor kBackground(8, 9, 12);
const QColor kGraticule(200, 170, 60, 70);  // Resolve-like amber lines
const QColor kLabel(200, 170, 60);

/// Brightness of a trace cell from its count (log-ish so faint detail shows).
int glow(std::uint32_t count, int perColumn) {
    if (count == 0) return 0;
    const double v = std::log1p(static_cast<double>(count)) / std::log1p(std::max(1.0, perColumn * 0.15));
    return std::clamp(static_cast<int>(v * 255.0), 40, 255);
}
}  // namespace

ScopeItem::ScopeItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    setFillColor(kBackground);
}

void ScopeItem::setPlayback(PlaybackController* playback) {
    if (playback_ == playback) return;
    if (playback_) disconnect(playback_, nullptr, this, nullptr);
    playback_ = playback;
    if (playback_) {
        connect(playback_, &PlaybackController::frameReady, this, [this] {
            if (isVisible() && playback_) analyze(playback_->frame());
        });
        analyze(playback_->frame());
    }
    emit playbackChanged();
}

void ScopeItem::setMode(const QString& mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    emit modeChanged();
    update();
}

void ScopeItem::analyze(const QImage& frame) {
    if (frame.isNull()) return;
    const QImage small = frame.scaledToWidth(std::min(kColumns, frame.width()), Qt::FastTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    columns_ = small.width();
    samplesPerColumn_ = std::max(1, small.height());
    luma_.assign(static_cast<std::size_t>(columns_), {});
    for (auto& c : rgb_) c.assign(static_cast<std::size_t>(columns_), {});
    for (auto& h : histogram_) h.fill(0);
    vector_ = QImage(256, 256, QImage::Format_Grayscale8);
    std::vector<std::uint32_t> density(256 * 256, 0);
    int black = 255;
    int white = 0;
    long long clipped = 0;
    double sumR = 0, sumG = 0, sumB = 0;
    for (int y = 0; y < small.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(small.constScanLine(y));
        for (int x = 0; x < columns_; ++x) {
            const int r = qRed(row[x]);
            const int g = qGreen(row[x]);
            const int b = qBlue(row[x]);
            const int l = std::clamp(static_cast<int>(std::lround(0.2126 * r + 0.7152 * g + 0.0722 * b)), 0, 255);
            auto& col = luma_[static_cast<std::size_t>(x)];
            col[static_cast<std::size_t>(l)]++;
            rgb_[0][static_cast<std::size_t>(x)][static_cast<std::size_t>(r)]++;
            rgb_[1][static_cast<std::size_t>(x)][static_cast<std::size_t>(g)]++;
            rgb_[2][static_cast<std::size_t>(x)][static_cast<std::size_t>(b)]++;
            histogram_[0][static_cast<std::size_t>(l)]++;
            histogram_[1][static_cast<std::size_t>(r)]++;
            histogram_[2][static_cast<std::size_t>(g)]++;
            histogram_[3][static_cast<std::size_t>(b)]++;
            // BT.709 Cb/Cr, centered, ±0.5 → 0…255
            const double cb = (-0.1146 * r - 0.3854 * g + 0.5 * b) / 255.0;
            const double cr = (0.5 * r - 0.4542 * g - 0.0458 * b) / 255.0;
            const int vx = std::clamp(static_cast<int>(128 + cb * 255), 0, 255);
            const int vy = std::clamp(static_cast<int>(128 - cr * 255), 0, 255);
            density[static_cast<std::size_t>(vy * 256 + vx)]++;
            sumR += r;
            sumG += g;
            sumB += b;
            black = std::min(black, l);
            white = std::max(white, l);
            if (r >= 254 || g >= 254 || b >= 254) ++clipped;
        }
    }
    const std::uint32_t peak = std::max<std::uint32_t>(1, *std::max_element(density.begin(), density.end()));
    for (int y = 0; y < 256; ++y) {
        auto* line = vector_.scanLine(y);
        for (int x = 0; x < 256; ++x) {
            const std::uint32_t d = density[static_cast<std::size_t>(y * 256 + x)];
            line[x] = d == 0 ? 0 : static_cast<uchar>(std::clamp(60.0 + 195.0 * std::log1p(d) / std::log1p(peak), 0.0, 255.0));
        }
    }
    const double pixels = static_cast<double>(columns_) * samplesPerColumn_;
    stats_ = QVariantMap{{"black", black * 1023 / 255},
                         {"white", white * 1023 / 255},
                         {"clippedPercent", 100.0 * static_cast<double>(clipped) / std::max(1.0, pixels)},
                         {"meanR", sumR / std::max(1.0, pixels) / 255.0},
                         {"meanG", sumG / std::max(1.0, pixels) / 255.0},
                         {"meanB", sumB / std::max(1.0, pixels) / 255.0}};
    emit statsChanged();
    update();
}

void ScopeItem::paint(QPainter* p) {
    const QRectF area = boundingRect().adjusted(34, 8, -8, -8);
    p->setRenderHint(QPainter::Antialiasing, false);
    QFont f = p->font();
    f.setPixelSize(10);
    p->setFont(f);
    auto levelY = [&](double v) { return area.bottom() - v * area.height(); };  // v: 0…1

    if (mode_ == QLatin1String("vectorscope")) {
        const double side = std::min(area.width(), area.height());
        const QRectF box(area.center().x() - side / 2, area.center().y() - side / 2, side, side);
        p->setPen(kGraticule);
        p->drawEllipse(box);
        p->drawLine(QPointF(box.center().x(), box.top()), QPointF(box.center().x(), box.bottom()));
        p->drawLine(QPointF(box.left(), box.center().y()), QPointF(box.right(), box.center().y()));
        // Skin-tone line (≈ 123° on a Rec.709 vectorscope).
        const double a = 123.0 * 3.14159265358979323846 / 180.0;
        p->drawLine(box.center(), box.center() + QPointF(std::cos(a) * side / 2, -std::sin(a) * side / 2));
        // 75 % color targets.
        const std::array<std::pair<const char*, QColor>, 6> targets{
            {{"R", QColor(191, 0, 0)}, {"Mg", QColor(191, 0, 191)}, {"B", QColor(0, 0, 191)},
             {"Cy", QColor(0, 191, 191)}, {"G", QColor(0, 191, 0)}, {"Yl", QColor(191, 191, 0)}}};
        for (const auto& [name, c] : targets) {
            const double cb = (-0.1146 * c.red() - 0.3854 * c.green() + 0.5 * c.blue()) / 255.0;
            const double cr = (0.5 * c.red() - 0.4542 * c.green() - 0.0458 * c.blue()) / 255.0;
            const QPointF pt(box.center().x() + cb * side, box.center().y() - cr * side);
            p->drawRect(QRectF(pt - QPointF(4, 4), QSizeF(8, 8)));
            p->setPen(kLabel);
            p->drawText(pt + QPointF(6, 4), QString::fromLatin1(name));
            p->setPen(kGraticule);
        }
        if (!vector_.isNull()) {
            QImage tinted(vector_.size(), QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < 256; ++y) {
                const auto* in = vector_.constScanLine(y);
                auto* out = reinterpret_cast<QRgb*>(tinted.scanLine(y));
                for (int x = 0; x < 256; ++x) out[x] = in[x] ? qRgba(in[x] * 9 / 10, in[x], in[x] * 9 / 10, in[x]) : 0;
            }
            p->setRenderHint(QPainter::SmoothPixmapTransform);
            p->drawImage(box, tinted);
        }
        return;
    }

    // Graticule: 0 … 1023 like Resolve.
    for (int v : {0, 128, 256, 384, 512, 640, 768, 896, 1023}) {
        const double y = levelY(v / 1023.0);
        p->setPen(kGraticule);
        p->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p->setPen(kLabel);
        p->drawText(QRectF(0, y - 6, area.left() - 4, 12), Qt::AlignRight | Qt::AlignVCenter, QString::number(v));
    }
    if (columns_ == 0) return;

    if (mode_ == QLatin1String("histogram")) {
        const std::array<QColor, 4> colors{QColor(220, 220, 220, 140), QColor(240, 70, 70, 140), QColor(70, 220, 90, 140),
                                           QColor(80, 130, 255, 140)};
        std::uint32_t peak = 1;
        for (int c = 1; c < 4; ++c) peak = std::max(peak, *std::max_element(histogram_[static_cast<std::size_t>(c)].begin(), histogram_[static_cast<std::size_t>(c)].end()));
        p->setRenderHint(QPainter::Antialiasing, true);
        for (int c = 1; c < 4; ++c) {
            QPainterPath path(QPointF(area.left(), area.bottom()));
            for (int i = 0; i < kLevels; ++i) {
                const double x = area.left() + area.width() * i / (kLevels - 1);
                const double h = std::sqrt(static_cast<double>(histogram_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]) / peak);
                path.lineTo(x, area.bottom() - h * area.height());
            }
            path.lineTo(area.right(), area.bottom());
            p->fillPath(path, colors[static_cast<std::size_t>(c)]);
        }
        return;
    }

    // Waveform / parade: each column's level counts become a glowing trace.
    const bool parade = mode_ == QLatin1String("parade");
    const int panels = parade ? 3 : 1;
    const double panelWidth = area.width() / panels;
    QImage trace(columns_ * panels, kLevels, QImage::Format_ARGB32_Premultiplied);
    trace.fill(Qt::transparent);
    for (int panel = 0; panel < panels; ++panel) {
        const auto& data = parade ? rgb_[static_cast<std::size_t>(panel)] : luma_;
        for (int x = 0; x < columns_; ++x) {
            for (int lv = 0; lv < kLevels; ++lv) {
                const int a = glow(data[static_cast<std::size_t>(x)][static_cast<std::size_t>(lv)], samplesPerColumn_);
                if (a == 0) continue;
                QRgb color = qRgba(a * 9 / 10, a, a * 9 / 10, a);
                if (parade) {
                    color = panel == 0 ? qRgba(a, a / 5, a / 5, a) : panel == 1 ? qRgba(a / 5, a, a / 4, a) : qRgba(a / 4, a / 3, a, a);
                }
                trace.setPixel(panel * columns_ + x, kLevels - 1 - lv, color);
            }
        }
    }
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    for (int panel = 0; panel < panels; ++panel) {
        p->drawImage(QRectF(area.left() + panel * panelWidth + (parade ? 2 : 0), area.top(), panelWidth - (parade ? 4 : 0), area.height()),
                     trace, QRectF(panel * columns_, 0, columns_, kLevels));
    }
}

}  // namespace lectern::ui
