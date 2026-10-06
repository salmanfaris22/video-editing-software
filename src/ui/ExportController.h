#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <thread>

namespace lectern::ui {

class ProjectController;

/// Exports the open project on a background thread (editor::exportProject),
/// with progress, cancel and "show in Finder".
class ExportController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppController")

    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool finished READ finished NOTIFY stateChanged)

public:
    explicit ExportController(ProjectController* project, QObject* parent = nullptr);
    ~ExportController() override;

    /// Sizes for the current canvas: [{id, label, width, height}], shorter side 720…2160.
    Q_INVOKABLE QVariantList resolutions() const;
    /// Default export path (Movies/<product>/<title>.<ext>, made unique). `format`: mp4, mkv, mov.
    Q_INVOKABLE QString suggestedPath(const QString& format = QStringLiteral("mp4")) const;
    /// Accepts a local path or a file:// URL (from the save dialog).
    Q_INVOKABLE void start(const QString& resolution, int fps, const QString& quality, const QString& path = {},
                           const QString& format = QStringLiteral("mp4"), double bitrateScale = 1.0);
    /// Approximate file size of an export of the whole timeline.
    Q_INVOKABLE double estimateMegabytes(const QString& resolution, int fps, const QString& quality,
                                         double bitrateScale = 1.0) const;
    /// Human-readable codec line for the export dialog.
    Q_INVOKABLE QString formatSummary(const QString& format) const;
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reveal() const;
    Q_INVOKABLE void reset();

    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] double progress() const { return progress_; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] QString outputPath() const { return outputPath_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] bool finished() const { return finished_; }

signals:
    void stateChanged();
    void progressChanged();

private:
    void join();

    ProjectController* project_;
    std::thread thread_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    bool running_ = false;
    bool finished_ = false;
    double progress_ = 0;
    QString status_;
    QString outputPath_;
    QString error_;
};

}  // namespace lectern::ui
