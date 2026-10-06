#include "ui/ExportController.h"

#include "editor/Exporter.h"
#include "ui/ProjectController.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <cmath>

namespace lectern::ui {

namespace {

QString sanitize(QString title) {
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (QChar& c : title) {
        if (forbidden.contains(c) || c.unicode() < 32) c = QLatin1Char('-');
    }
    title = title.trimmed();
    return title.isEmpty() ? QStringLiteral("Video") : title.left(120);
}

QString normalizeFormat(QString format) {
    const QString f = format.trimmed().toLower();
    if (f == QLatin1String("mkv") || f == QLatin1String("matroska")) return QStringLiteral("mkv");
    if (f == QLatin1String("mov")) return QStringLiteral("mov");
    return QStringLiteral("mp4");
}

QString extensionForFormat(const QString& format) {
    const QString f = normalizeFormat(format);
    if (f == QLatin1String("mkv")) return QStringLiteral(".mkv");
    if (f == QLatin1String("mov")) return QStringLiteral(".mov");
    return QStringLiteral(".mp4");
}

QString pathWithFormatExtension(QString path, const QString& format) {
    if (path.isEmpty()) return path;
    if (path.startsWith(QLatin1String("file:"))) path = QUrl(path).toLocalFile();
    const QString ext = extensionForFormat(format);
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".mp4")) || lower.endsWith(QStringLiteral(".mkv")) ||
        lower.endsWith(QStringLiteral(".mov"))) {
        const int dot = path.lastIndexOf(QLatin1Char('.'));
        return path.left(dot) + ext;
    }
    return path + ext;
}

}  // namespace

ExportController::ExportController(ProjectController* project, QObject* parent) : QObject(parent), project_(project) {}

ExportController::~ExportController() {
    if (cancel_) cancel_->store(true);
    join();
}

void ExportController::join() {
    if (thread_.joinable()) thread_.join();
}

QVariantList ExportController::resolutions() const {
    const int w = project_->canvasWidth();
    const int h = project_->canvasHeight();
    const int shorter = std::max(2, std::min(w, h));
    QVariantList out;
    for (const int target : {720, 1080, 1440, 2160}) {
        const double scale = static_cast<double>(target) / shorter;
        const int ew = static_cast<int>(std::lround(w * scale / 2.0)) * 2;
        const int eh = static_cast<int>(std::lround(h * scale / 2.0)) * 2;
        const QString id = target == 2160 ? QStringLiteral("4K") : QStringLiteral("%1p").arg(target);
        out.append(QVariantMap{{"id", id}, {"label", QStringLiteral("%1 · %2 × %3").arg(id).arg(ew).arg(eh)},
                               {"width", ew}, {"height", eh}});
    }
    return out;
}

QString ExportController::suggestedPath(const QString& format) const {
    const QString ext = extensionForFormat(format);
    QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty()) base = QDir::homePath();
    const QString dir = QDir(base).filePath(QStringLiteral(LECTERN_PRODUCT_NAME));
    const QString name = sanitize(project_->title());
    QString path = QDir(dir).filePath(name + ext);
    for (int i = 2; QFileInfo::exists(path); ++i) path = QDir(dir).filePath(QStringLiteral("%1 %2%3").arg(name).arg(i).arg(ext));
    return path;
}

QString ExportController::formatSummary(const QString& format) const {
    const QString f = normalizeFormat(format);
    if (f == QLatin1String("mkv")) return QStringLiteral("H.264 + AAC · Matroska (MKV)");
    if (f == QLatin1String("mov")) return QStringLiteral("H.264 + AAC · QuickTime (MOV)");
    return QStringLiteral("H.264 + AAC · MP4");
}

void ExportController::start(const QString& resolution, int fps, const QString& quality, const QString& path,
                             const QString& format, double bitrateScale) {
    if (running_ || !project_->snapshot()) return;
    join();
    QVariantMap size;
    for (const QVariant& r : resolutions()) {
        if (r.toMap().value("id").toString() == resolution) size = r.toMap();
    }
    if (size.isEmpty()) size = resolutions().value(1).toMap();  // 1080p

    const QString container = normalizeFormat(format);
    QString target = path.isEmpty() ? suggestedPath(container) : pathWithFormatExtension(path, container);
    if (target.startsWith(QLatin1String("file:"))) target = QUrl(target).toLocalFile();
    editor::ExportOptions options;
    options.output = target.toStdString();
    options.width = size.value("width").toInt();
    options.height = size.value("height").toInt();
    options.frameRate = FrameRate(std::clamp(fps, 1, 120), 1);
    options.quality = quality.toStdString();
    options.bitrateScale = std::clamp(bitrateScale, 0.25, 2.0);
    options.container = container.toStdString();

    cancel_ = std::make_shared<std::atomic<bool>>(false);
    running_ = true;
    finished_ = false;
    progress_ = 0;
    error_.clear();
    outputPath_ = QString::fromStdString(options.output.string());
    status_ = QStringLiteral("Preparing…");
    emit stateChanged();
    emit progressChanged();

    thread_ = std::thread([this, snapshot = project_->snapshot(), dir = project_->directory(), options, cancel = cancel_] {
        auto result = editor::exportProject(*snapshot, dir, options, [this](const editor::ExportProgress& p) {
            QMetaObject::invokeMethod(this, [this, p] {
                progress_ = p.fraction;
                status_ = QStringLiteral("Exporting… %1% · %2× real time")
                              .arg(static_cast<int>(p.fraction * 100))
                              .arg(p.speed, 0, 'f', 1);
                emit progressChanged();
            }, Qt::QueuedConnection);
        }, cancel.get());
        QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
            running_ = false;
            if (result) {
                finished_ = true;
                progress_ = 1;
                status_ = QStringLiteral("Exported %1 (%2 MB) in %3 s with %4")
                              .arg(QFileInfo(outputPath_).fileName())
                              .arg(static_cast<double>(result->bytes) / 1e6, 0, 'f', 1)
                              .arg(result->seconds, 0, 'f', 1)
                              .arg(QString::fromStdString(result->videoEncoder));
            } else if (result.error().code() == ErrorCode::Cancelled) {
                status_ = QStringLiteral("Export cancelled");
                outputPath_.clear();
            } else {
                error_ = QString::fromStdString(result.error().message());
                status_ = QStringLiteral("Export failed");
                outputPath_.clear();
            }
            emit stateChanged();
            emit progressChanged();
        }, Qt::QueuedConnection);
    });
}

double ExportController::estimateMegabytes(const QString& resolution, int fps, const QString& quality,
                                           double bitrateScale) const {
    QVariantMap size;
    for (const QVariant& r : resolutions()) {
        if (r.toMap().value("id").toString() == resolution) size = r.toMap();
    }
    if (size.isEmpty()) return 0;
    const auto video = editor::exportBitrate(size.value("width").toInt(), size.value("height").toInt(),
                                             FrameRate(std::clamp(fps, 1, 120), 1), quality.toStdString(), bitrateScale);
    return (static_cast<double>(video) + 192'000.0) * project_->duration() / 8.0 / 1e6;
}

void ExportController::cancel() {
    if (cancel_) cancel_->store(true);
}

void ExportController::reset() {
    if (running_) return;
    finished_ = false;
    error_.clear();
    status_.clear();
    progress_ = 0;
    emit stateChanged();
    emit progressChanged();
}

void ExportController::reveal() const {
    if (outputPath_.isEmpty()) return;
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), outputPath_});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer"), {QStringLiteral("/select,"), QDir::toNativeSeparators(outputPath_)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outputPath_).absolutePath()));
#endif
}

}  // namespace lectern::ui
