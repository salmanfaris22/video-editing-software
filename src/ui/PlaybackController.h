#pragma once

#include "editor/PlaybackEngine.h"

#include <QImage>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <filesystem>
#include <memory>
#include <mutex>

namespace lectern::ui {

class ProjectController;

/// Preview playback for the editor: owns the PlaybackEngine of the open
/// project, follows the document's snapshots, and exposes play state, the
/// playhead and audio levels to QML. Frames go to EditorPreviewItem.
class PlaybackController : public QObject, private editor::PlaybackEngine::Listener {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppController")

    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double levelLeft READ levelLeft NOTIFY levelsChanged)
    Q_PROPERTY(double levelRight READ levelRight NOTIFY levelsChanged)
    Q_PROPERTY(QString outputName READ outputName NOTIFY outputChanged)
    /// Before / after view of the grade: "off", "bypass", "wipe", "side".
    Q_PROPERTY(QString compareMode READ compareMode WRITE setCompareMode NOTIFY compareChanged)
    /// Wipe position, 0…1 of the picture width (ungraded to the left).
    Q_PROPERTY(double compareSplit READ compareSplit WRITE setCompareSplit NOTIFY compareChanged)

public:
    /// `silent` uses no audio device (synthetic sessions, automated runs).
    PlaybackController(ProjectController* project, bool silent, QObject* parent = nullptr);
    ~PlaybackController() override;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(double seconds);
    /// Pauses and moves by whole frames (negative = back).
    Q_INVOKABLE void step(int frames);

    [[nodiscard]] bool playing() const { return playing_; }
    [[nodiscard]] double position() const { return position_; }
    [[nodiscard]] double levelLeft() const { return levels_[0]; }
    [[nodiscard]] double levelRight() const { return levels_[1]; }
    [[nodiscard]] QString outputName() const { return outputName_; }
    [[nodiscard]] QString compareMode() const { return compareMode_; }
    void setCompareMode(const QString& mode);
    [[nodiscard]] double compareSplit() const { return compareSplit_; }
    void setCompareSplit(double split);

    /// Latest rendered canvas (any thread).
    [[nodiscard]] QImage frame() const;
    /// Pixel size the preview is shown at (frames are rendered at this size).
    void setPreviewSize(QSize pixels);

signals:
    void playingChanged();
    void positionChanged();
    void levelsChanged();
    void outputChanged();
    void frameReady();
    void compareChanged();

private:
    void onFrame(const QImage& frame, Time time) override;
    void onPlayingChanged(bool playing) override;
    void follow();
    void tick();
    void applyCompare();

    ProjectController* project_;
    bool silent_;
    std::filesystem::path dir_;
    std::unique_ptr<editor::PlaybackEngine> engine_;
    QTimer ticker_;
    QSize previewSize_{960, 540};
    mutable std::mutex frameMutex_;
    QImage frame_;
    bool playing_ = false;
    double position_ = 0;
    double levels_[2] = {0, 0};
    QString outputName_;
    QString compareMode_ = QStringLiteral("off");
    double compareSplit_ = 0.5;
};

}  // namespace lectern::ui
