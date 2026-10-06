#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <vector>

namespace lectern::ui {

class PlaybackController;

/// Video scopes of the preview frame (Color page): "waveform" (luma),
/// "parade" (R, G, B side by side), "vectorscope" (Cb/Cr with Rec.709
/// targets and the skin-tone line) and "histogram". Recomputed on every new
/// frame from a ~320 px copy, so it costs about a millisecond.
class ScopeItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(PlaybackController* playback READ playback WRITE setPlayback NOTIFY playbackChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    /// Only the histogram, faint, over the whole item and no graticule — the
    /// backdrop of the curve editor (as in Resolve's Curves palette).
    Q_PROPERTY(bool bare READ bare WRITE setBare NOTIFY bareChanged)
    /// Summary numbers (for tests and the MCP agent): black/white levels, clipping.
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)

public:
    explicit ScopeItem(QQuickItem* parent = nullptr);
    [[nodiscard]] PlaybackController* playback() const { return playback_; }
    void setPlayback(PlaybackController* playback);
    [[nodiscard]] QString mode() const { return mode_; }
    void setMode(const QString& mode);
    [[nodiscard]] QVariantMap stats() const { return stats_; }
    [[nodiscard]] bool bare() const { return bare_; }
    void setBare(bool bare);
    void paint(QPainter* painter) override;

    /// Computes the scope data for `frame` (exposed for tests).
    void analyze(const QImage& frame);

signals:
    void playbackChanged();
    void modeChanged();
    void statsChanged();
    void bareChanged();

private:
    static constexpr int kColumns = 320;
    static constexpr int kLevels = 256;
    QPointer<PlaybackController> playback_;
    QString mode_ = QStringLiteral("parade");
    bool bare_ = false;
    QVariantMap stats_;
    // Per column, counts per level: luma and R, G, B.
    std::vector<std::array<std::uint16_t, kLevels>> luma_;
    std::array<std::vector<std::array<std::uint16_t, kLevels>>, 3> rgb_;
    std::array<std::array<std::uint32_t, kLevels>, 4> histogram_{};  // Y, R, G, B
    QImage vector_;  ///< 256 × 256 Cb/Cr density
    int columns_ = 0;
    int samplesPerColumn_ = 1;
};

}  // namespace lectern::ui
