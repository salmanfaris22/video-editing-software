#include "ui/AppController.h"

#include "capture/synthetic/SyntheticSources.h"
#include "core/Log.h"
#include "platform/PlatformBackends.h"
#include "services/RecordingImporter.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>

namespace lectern::ui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

AppController::AppController(Options options, QObject* parent)
    : QObject(parent), options_(options), worker_(std::make_unique<SerialExecutor>("lectern.ui.app")) {
    if (auto loaded = SettingsStore(SettingsStore::defaultPath()).load()) {
        settings_ = *loaded;
    } else {
        LEC_WARN("app", "settings unreadable, using defaults: {}", loaded.error().toString());
    }
    backends_ = options_.synthetic ? capture::makeSyntheticBackends() : platform::createCaptureBackends();
    platformName_ = qs(platform::platformInfo().os);

    recorder_ = new RecorderController(backends_, settings_, [this] { saveSettings(); }, this);
    project_ = new ProjectController(this);
    // Automated runs (synthetic sources, offscreen screenshots) never play sound out loud.
    const bool silent = options_.synthetic || qEnvironmentVariable("QT_QPA_PLATFORM") == QLatin1String("offscreen");
    playback_ = new PlaybackController(project_, silent, this);
    exporter_ = new ExportController(project_, this);
    assistants_ = new McpController(project_, this);
    assistants_->enableAppTools({[this] { return recentProjects(); }, [this](const QString& dir) { openProject(dir); }});

    connect(recorder_, &RecorderController::recordingFinished, this, [this](const QString& dir) { openProject(dir); });
    connect(project_, &ProjectController::opened, this, [this](const QString& dir) {
        addRecent(dir);
        setView(QStringLiteral("editor"));
    });
    connect(project_, &ProjectController::failed, this, [this](const QString& message) {
        banner_ = QStringLiteral("Could not open project: ") + message;
        bannerProject_.clear();
        emit bannerChanged();
    });
    // Coming back from System Settings: pick up permission changes at once.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) recorder_->recheckPermissions();
    });
    checkForInterruptedRecordings();
    registerGlobalHotkeys();
}

AppController::~AppController() {
    hotkeys_.reset();  // callbacks capture `this`
    worker_.reset();  // finishes pending settings writes
    (void)SettingsStore(SettingsStore::defaultPath()).save(settings_);
}

QString AppController::productName() const { return QStringLiteral(LECTERN_PRODUCT_NAME); }
QString AppController::version() const { return QStringLiteral(LECTERN_VERSION); }

void AppController::setView(const QString& view) {
    if (view_ == view) return;
    if (view_ == QLatin1String("record") && view != QLatin1String("record")) recorder_->deactivate();
    if (view_ == QLatin1String("editor") && view != QLatin1String("editor")) playback_->pause();
    view_ = view;
    if (view_ == QLatin1String("record")) recorder_->activate();
    emit viewChanged();
}

void AppController::goHome() {
    if (recorder_->state() != QLatin1String("idle")) return;  // never leave a running recording
    setView(QStringLiteral("home"));
}

void AppController::newRecording() { setView(QStringLiteral("record")); }

void AppController::toggleRecording() {
    const QString state = recorder_->state();
    if (state == QLatin1String("idle")) {
        if (view_ != QLatin1String("record")) {
            newRecording();
            emit activateRequested();
        } else {
            recorder_->startRecording();
        }
    } else if (state == QLatin1String("countdown") || state == QLatin1String("preparing")) {
        recorder_->cancelRecording();
    } else {
        recorder_->stopRecording();
    }
}

void AppController::registerGlobalHotkeys() {
    if (options_.synthetic) return;  // keep automated/test runs from grabbing system shortcuts
    hotkeys_ = platform::createGlobalHotkeys();
    if (!hotkeys_) return;
    auto record = hotkeys_->add(1, {'R', platform::kHotkeyCommand | platform::kHotkeyShift}, [this] { toggleRecording(); });
    auto pause = hotkeys_->add(2, {'P', platform::kHotkeyCommand | platform::kHotkeyShift},
                               [this] { recorder_->togglePause(); });
    if (!record || !pause) {
        // Fall back to in-app shortcuts so a key press is never handled twice.
        LEC_WARN("app", "global hotkeys unavailable ({}); using in-app shortcuts",
                 (!record ? record.error() : pause.error()).message());
        hotkeys_->removeAll();
        hotkeys_.reset();
        return;
    }
    globalHotkeys_ = true;
    LEC_INFO("app", "global hotkeys: ⌘⇧R record/stop, ⌘⇧P pause/resume");
}

void AppController::openProject(const QString& dir) {
    if (dir.isEmpty()) return;
    project_->open(dir);
}

void AppController::openProjectUrl(const QUrl& url) {
    QString path = url.toLocalFile();
    if (QFileInfo(path).fileName() == QLatin1String("project.json")) path = QFileInfo(path).absolutePath();
    openProject(path);
}

void AppController::dismissBanner() {
    banner_.clear();
    bannerProject_.clear();
    emit bannerChanged();
}

void AppController::revealInFinder(const QString& path) {
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

QVariant AppController::workspaceValue(const QString& key, const QVariant& fallback) const {
    return workspace_.value(QStringLiteral("workspace/") + key, fallback);
}

void AppController::setWorkspaceValue(const QString& key, const QVariant& value) {
    workspace_.setValue(QStringLiteral("workspace/") + key, value);
}

QVariantList AppController::recentProjects() const {
    QVariantList out;
    for (const QString& dir : workspace_.value(QStringLiteral("recentProjects")).toStringList()) {
        if (!QFileInfo::exists(dir + QStringLiteral("/project.json"))) continue;
        const QFileInfo info(dir);
        QString name = info.fileName();
        if (name.endsWith(QStringLiteral(".lectern"))) name.chop(8);
        out.append(QVariantMap{{"path", dir},
                               {"name", name},
                               {"modified", info.lastModified().toString(QStringLiteral("d MMM yyyy, HH:mm"))}});
    }
    return out;
}

void AppController::addRecent(const QString& dir) {
    QStringList list = workspace_.value(QStringLiteral("recentProjects")).toStringList();
    list.removeAll(dir);
    list.prepend(dir);
    while (list.size() > 12) list.removeLast();
    workspace_.setValue(QStringLiteral("recentProjects"), list);
    emit recentProjectsChanged();
}

void AppController::saveSettings() {
    const AppSettings copy = settings_;
    worker_->post([copy] {
        if (auto st = SettingsStore(SettingsStore::defaultPath()).save(copy); !st) {
            LEC_WARN("app", "saving settings failed: {}", st.error().toString());
        }
    });
}

void AppController::checkForInterruptedRecordings() {
    // Runs off the UI thread: salvaging media can take a while for long recordings.
    worker_->post([this] {
        auto recovered = services::recoverInterruptedRecordings();
        if (!recovered || recovered->empty()) return;
        const auto& first = recovered->front();
        const QString dir = first.project ? qs(first.projectDir.string()) : QString();
        const QString text =
            first.project
                ? QStringLiteral("Recovered an interrupted recording (%1 saved).")
                      .arg(qs(lectern::formatDuration(first.report.manifest.duration)))
                : QStringLiteral("An interrupted recording could not be recovered.");
        QMetaObject::invokeMethod(this, [this, text, dir] {
            banner_ = text;
            bannerProject_ = dir;
            emit bannerChanged();
        }, Qt::QueuedConnection);
    });
}

}  // namespace lectern::ui
