#include "ui/RecorderController.h"

#include "core/Clock.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "mobile/PhoneCameraSource.h"
#include "platform/PlatformBackends.h"
#include "project/ProjectStore.h"
#include "services/RecordingImporter.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QWindow>
#include <QProcess>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace lectern::ui {

using namespace capture;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

qreal meterLevel(float db) {
    // -60 dBFS .. 0 dBFS → 0 .. 1 (speech typically sits around 0.6–0.8).
    return std::clamp((static_cast<qreal>(db) + 60.0) / 60.0, 0.0, 1.0);
}

std::optional<ScreenCaptureTarget> parseTarget(const QString& id) {
    const QString kind = id.section(QLatin1Char(':'), 0, 0);
    const std::string value = id.section(QLatin1Char(':'), 1).toStdString();
    if (value.empty()) return std::nullopt;
    if (kind == QLatin1String("display")) return ScreenCaptureTarget{ScreenTargetKind::Display, value, {}};
    if (kind == QLatin1String("window")) return ScreenCaptureTarget{ScreenTargetKind::Window, value, {}};
    if (kind == QLatin1String("app")) return ScreenCaptureTarget{ScreenTargetKind::Application, value, "main"};
    return std::nullopt;
}

PermissionKind permissionKindFrom(const QString& kind) {
    if (kind == QLatin1String("camera")) return PermissionKind::Camera;
    if (kind == QLatin1String("microphone")) return PermissionKind::Microphone;
    return PermissionKind::ScreenCapture;
}

}  // namespace

/// Forwards engine-thread notifications to the UI thread.
class RecorderController::Observer final : public IRecordingObserver {
public:
    explicit Observer(RecorderController* owner) : owner_(owner) {}
    void onWarning(const std::string& code, const std::string& message) override {
        QMetaObject::invokeMethod(owner_, [o = owner_, code, message] {
            (void)code;
            o->warnings_.append(qs(message));
            emit o->warningsChanged();
        }, Qt::QueuedConnection);
    }
    void onTrackFailed(const std::string& trackId, const Error& error) override {
        const std::string text = trackId + ": " + error.message();
        QMetaObject::invokeMethod(owner_, [o = owner_, text] {
            o->warnings_.append(qs(text));
            emit o->warningsChanged();
        }, Qt::QueuedConnection);
    }
    void onFinished(const SessionResult& result) override {
        QMetaObject::invokeMethod(owner_, [o = owner_, result] { o->onSessionFinished(result); }, Qt::QueuedConnection);
    }

private:
    RecorderController* owner_;
};

RecorderController::RecorderController(CaptureBackends& backends, AppSettings& settings,
                                       std::function<void()> settingsChanged, QObject* parent)
    : QObject(parent),
      backends_(backends),
      settings_(settings),
      settingsChanged_(std::move(settingsChanged)),
      worker_(std::make_unique<SerialExecutor>("lectern.ui.recorder")),
      observer_(std::make_unique<Observer>(this)) {
    const auto& r = settings_.recording;
    screenTarget_ = qs(r.lastScreenTarget);
    cameraId_ = qs(r.lastCameraId);
    if (cameraId_ == QLatin1String("phone")) phoneCameraEnabled_ = true;
    microphoneId_ = qs(r.lastMicrophoneId);
    systemAudio_ = r.captureSystemAudio;
    resolution_ = qs(r.resolution);
    frameRate_ = r.frameRate;
    countdownEnabled_ = r.countdown;

    tick_.setInterval(50);  // meters at 20 Hz, stats at 10 Hz
    connect(&tick_, &QTimer::timeout, this, &RecorderController::onTick);
    countdownTimer_.setInterval(1000);
    connect(&countdownTimer_, &QTimer::timeout, this, [this] {
        countdown_ -= 1;
        emit countdownChanged();
        if (countdown_ <= 0) {
            countdownTimer_.stop();
            beginRecordingNow();
        }
    });
}

RecorderController::~RecorderController() {
    tick_.stop();
    countdownTimer_.stop();
    auto session = std::move(sharedSession_);
    worker_->post([this, session = std::move(session)]() mutable {
        if (session && (session->state() == SessionState::Recording || session->state() == SessionState::Paused)) {
            (void)session->stop();
            (void)session->waitForCompletion(std::chrono::seconds(30));
        }
        session.reset();
        sources_.stopAll();
    });
    worker_.reset();  // joins after the shutdown task
}

QString RecorderController::permission(PermissionKind kind) const {
    if (!backends_.permissions) return QStringLiteral("granted");
    return qs(std::string(toString(backends_.permissions->status(kind))));
}

void RecorderController::setState(const QString& s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged();
}

void RecorderController::setError(const QString& message) {
    errorMessage_ = message;
    emit errorChanged();
}

void RecorderController::dismissError() { setError({}); }

void RecorderController::copyPhoneUrl() {
    if (phoneUrl_.isEmpty()) return;
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(phoneUrl_);
}

services::RecordingRequest RecorderController::buildRequest() const {
    services::RecordingRequest r;
    r.screen = parseTarget(screenTarget_);
    if (!cameraId_.isEmpty() && cameraId_ != QLatin1String("phone")) r.cameraId = cameraId_.toStdString();
    if (!microphoneId_.isEmpty()) r.microphoneId = microphoneId_ == QLatin1String("default") ? "" : microphoneId_.toStdString();
    r.systemAudio = systemAudio_ && systemAudioSupported_;
    r.phone = phoneCameraEnabled_ || cameraId_ == QLatin1String("phone");
    r.resolution = resolution_.toStdString();
    r.frameRate = frameRate_;
    r.quality = settings_.recording.quality;
    r.encoder = settings_.hardwareAcceleration ? settings_.recording.encoder : "software";
    r.showCursor = settings_.recording.showCursor;
    r.excludeOwnApplication = settings_.recording.excludeOwnWindows;
    return r;
}

void RecorderController::persistSelection() {
    auto& r = settings_.recording;
    r.lastScreenTarget = screenTarget_.toStdString();
    r.lastCameraId = cameraId_.toStdString();
    r.lastMicrophoneId = microphoneId_.toStdString();
    r.captureSystemAudio = systemAudio_;
    r.resolution = resolution_.toStdString();
    r.frameRate = frameRate_;
    r.countdown = countdownEnabled_;
    if (settingsChanged_) settingsChanged_();
}

// ---------------------------------------------------------------------------
// Selection

void RecorderController::setScreenTarget(const QString& v) {
    if (v == screenTarget_ || state_ != QLatin1String("idle")) return;
    screenTarget_ = v;
    persistSelection();
    emit selectionChanged();
    reopenScreen();
}

void RecorderController::setCameraId(const QString& v) {
    if (v == cameraId_ || state_ != QLatin1String("idle")) return;
    const bool wasPhone = cameraId_ == QLatin1String("phone");
    const bool isPhone = v == QLatin1String("phone");
    cameraId_ = v;
    phoneCameraEnabled_ = isPhone;
    persistSelection();
    emit selectionChanged();
    reopenCamera();
    if (wasPhone != isPhone) reopenPhone();
}

void RecorderController::setMicrophoneId(const QString& v) {
    if (v == microphoneId_ || state_ != QLatin1String("idle")) return;
    microphoneId_ = v;
    persistSelection();
    emit selectionChanged();
    reopenMicrophone();
}

void RecorderController::setSystemAudio(bool v) {
    if (v == systemAudio_ || state_ != QLatin1String("idle")) return;
    systemAudio_ = v;
    persistSelection();
    emit selectionChanged();
}

void RecorderController::setResolution(const QString& v) {
    if (v == resolution_ || state_ != QLatin1String("idle")) return;
    resolution_ = v;
    persistSelection();
    emit selectionChanged();
    if (!screenTarget_.isEmpty()) reopenScreen();
}

void RecorderController::setFrameRate(int v) {
    if (v == frameRate_ || state_ != QLatin1String("idle")) return;
    frameRate_ = v;
    persistSelection();
    emit selectionChanged();
    if (!screenTarget_.isEmpty()) reopenScreen();
}

void RecorderController::setCountdownEnabled(bool v) {
    if (v == countdownEnabled_) return;
    countdownEnabled_ = v;
    persistSelection();
    emit selectionChanged();
}

void RecorderController::setPhoneCameraEnabled(bool v) {
    if (v == phoneCameraEnabled_ || state_ != QLatin1String("idle")) return;
    phoneCameraEnabled_ = v;
    if (v && cameraId_ != QLatin1String("phone")) {
        cameraId_ = QStringLiteral("phone");
        persistSelection();
        emit selectionChanged();
        reopenCamera();
        reopenPhone();
        return;
    }
    if (!v && cameraId_ == QLatin1String("phone")) {
        cameraId_.clear();
        persistSelection();
        emit selectionChanged();
        reopenCamera();
    }
    reopenPhone();
}

// ---------------------------------------------------------------------------
// Screen lifecycle

void RecorderController::activate() {
    if (active_) return;
    active_ = true;
    tick_.start();
    refreshSources();
}

void RecorderController::deactivate() {
    if (!active_ || (state_ != QLatin1String("idle") && state_ != QLatin1String("error"))) return;
    active_ = false;
    tick_.stop();
    worker_->post([this] { sources_.stopAll(); sources_ = {}; });
    micLive_.reset();
    cameraLive_.reset();
    phoneLive_.reset();
    screenLive_.reset();
    if (screenPreview_) screenPreview_->deleteLater();
    screenPreview_ = nullptr;
    if (cameraPreview_) cameraPreview_->deleteLater();
    cameraPreview_ = nullptr;
    if (phonePreview_) phonePreview_->deleteLater();
    phonePreview_ = nullptr;
    emit previewChanged();
    screenStatus_ = QStringLiteral("Off");
    cameraStatus_ = QStringLiteral("Off");
    micStatus_ = QStringLiteral("Off");
    phoneStatus_ = QStringLiteral("Idle");
    phoneUrl_.clear();
    phoneFramesReceived_ = 0;
    emit statsChanged();
    emit phoneChanged();
}

void RecorderController::refreshSources() {
    loadingSources_ = true;
    emit loadingSourcesChanged();
    worker_->post([this] {
        QVariantList screens;
        QVariantList cams;
        QVariantList mics;
        QString mainDisplay;
        QString defaultMic;
        if (auto targets = backends_.screen->enumerateTargets()) {
            for (const auto& d : targets->displays) {
                const QString id = QStringLiteral("display:") + qs(d.id);
                if (d.isMain || mainDisplay.isEmpty()) mainDisplay = id;
                screens.append(QVariantMap{{"id", id},
                                           {"kind", "display"},
                                           {"name", qs(d.name)},
                                           {"detail", QStringLiteral("%1 × %2").arg(d.widthPx).arg(d.heightPx)}});
            }
            for (const auto& w : targets->windows) {
                screens.append(QVariantMap{{"id", QStringLiteral("window:") + qs(w.id)},
                                           {"kind", "window"},
                                           {"name", qs(w.title)},
                                           {"detail", qs(w.applicationName)}});
            }
            for (const auto& a : targets->applications) {
                screens.append(QVariantMap{{"id", QStringLiteral("app:") + qs(a.id)},
                                           {"kind", "app"},
                                           {"name", qs(a.name)},
                                           {"detail", QStringLiteral("All windows")}});
            }
        }
        cams.append(QVariantMap{{"id", QStringLiteral("phone")},
                                {"name", QStringLiteral("Phone camera")},
                                {"detail", QStringLiteral("Same Wi‑Fi · open the share link")}});
        if (auto list = backends_.camera->enumerateCameras()) {
            for (const auto& c : *list) {
                int maxW = 0;
                int maxH = 0;
                for (const auto& f : c.formats) {
                    if (f.width * f.height > maxW * maxH) {
                        maxW = f.width;
                        maxH = f.height;
                    }
                }
                cams.append(QVariantMap{{"id", qs(c.id)},
                                        {"name", qs(c.name)},
                                        {"detail", c.continuity ? QStringLiteral("Continuity Camera")
                                                                : QStringLiteral("Up to %1p").arg(maxH)}});
            }
        }
        if (auto list = backends_.audio->enumerateInputs()) {
            for (const auto& m : *list) {
                if (m.isDefault) defaultMic = qs(m.id);
                mics.append(QVariantMap{{"id", qs(m.id)},
                                        {"name", qs(m.name)},
                                        {"detail", qs(m.transport) + (m.isDefault ? QStringLiteral(" · default") : QString())}});
            }
        }
        const bool systemAudio = backends_.audio->supportsSystemAudio();
        QMetaObject::invokeMethod(this, [=, this] {
            screenTargets_ = screens;
            cameras_ = cams;
            microphones_ = mics;
            systemAudioSupported_ = systemAudio;
            loadingSources_ = false;
            // Keep valid persisted selections; otherwise choose sensible defaults.
            auto contains = [](const QVariantList& list, const QString& id) {
                return std::any_of(list.begin(), list.end(), [&](const QVariant& v) { return v.toMap().value("id") == id; });
            };
            if (!contains(screens, screenTarget_)) screenTarget_ = mainDisplay;
            if (!cameraId_.isEmpty() && cameraId_ != QLatin1String("phone") && !contains(cams, cameraId_)) cameraId_.clear();
            if (cameraId_ == QLatin1String("phone")) phoneCameraEnabled_ = true;
            if (cameraId_.isEmpty() && !cams.isEmpty() && settings_.recording.lastCameraId.empty() &&
                permission(PermissionKind::Camera) == QLatin1String("granted")) {
                cameraId_ = cams.first().toMap().value("id").toString();
            }
            if (microphoneId_.isEmpty() || (microphoneId_ != QLatin1String("default") && !contains(mics, microphoneId_))) {
                microphoneId_ = defaultMic.isEmpty() ? QString() : defaultMic;
            }
            emit sourcesChanged();
            emit loadingSourcesChanged();
            emit selectionChanged();
            emit permissionsChanged();
            reopenScreen();
            reopenCamera();
            reopenMicrophone();
            reopenPhone();
        }, Qt::QueuedConnection);
    });
}

void RecorderController::reopenScreen() {
    if (!active_) return;
    const QString target = screenTarget_;
    const bool allowed = permission(PermissionKind::ScreenCapture) == QLatin1String("granted");
    screenStatus_ = target.isEmpty() ? QStringLiteral("Off") : (allowed ? QStringLiteral("Starting…") : QStringLiteral("No access"));
    emit statsChanged();
    const services::RecordingRequest request = buildRequest();
    worker_->post([this, target, allowed, request] {
        if (sources_.screen) {
            sources_.screen->stop();
            sources_.screen.reset();
        }
        std::shared_ptr<LiveVideoSource> live;
        QString error;
        if (!target.isEmpty() && allowed) {
            services::RecordingRequest only;
            only.screen = parseTarget(target);
            only.resolution = request.resolution;
            only.frameRate = request.frameRate;
            only.showCursor = request.showCursor;
            only.excludeOwnApplication = request.excludeOwnApplication;
            if (only.screen) {
                if (auto s = services::openScreenSource(backends_, only); s) {
                    live = std::move(*s);
                } else {
                    error = qs(s.error().message());
                }
            } else {
                error = QStringLiteral("Invalid screen target");
            }
        }
        sources_.screen = live;
        QMetaObject::invokeMethod(this, [this, live, error] {
            screenLive_ = live;
            if (screenPreview_) screenPreview_->deleteLater();
            screenPreview_ = live ? new PreviewSource(live, this) : nullptr;
            emit previewChanged();
            if (!error.isEmpty()) {
                screenStatus_ = QStringLiteral("Unavailable");
                warnings_.append(QStringLiteral("Screen: ") + error);
                emit warningsChanged();
                emit statsChanged();
            }
        }, Qt::QueuedConnection);
    });
}

void RecorderController::reopenCamera() {
    if (!active_) return;
    const QString id = cameraId_;
    const bool allowed = permission(PermissionKind::Camera) == QLatin1String("granted");
    const auto request = buildRequest();
    cameraStatus_ = id.isEmpty() ? QStringLiteral("Off") : (allowed ? QStringLiteral("Starting…") : QStringLiteral("No access"));
    emit statsChanged();
    worker_->post([this, id, allowed, request] {
        if (sources_.camera) {
            sources_.camera->stop();
            sources_.camera.reset();
        }
        std::shared_ptr<LiveVideoSource> live;
        QString error;
        if (!id.isEmpty() && id != QLatin1String("phone") && allowed) {
            services::RecordingRequest only;
            only.cameraId = id.toStdString();
            only.resolution = request.resolution;
            only.frameRate = request.frameRate;
            auto set = services::openSources(backends_, only);
            if (set && set->camera) {
                live = set->camera;
            } else {
                error = set ? QString() : qs(set.error().message());
            }
        }
        sources_.camera = live;
        QMetaObject::invokeMethod(this, [this, live, error] {
            cameraLive_ = live;
            if (cameraPreview_) cameraPreview_->deleteLater();
            cameraPreview_ = live ? new PreviewSource(live, this) : nullptr;
            emit previewChanged();
            if (!error.isEmpty()) {
                cameraStatus_ = QStringLiteral("Unavailable");
                warnings_.append(QStringLiteral("Camera: ") + error);
                emit warningsChanged();
                emit statsChanged();
            }
        }, Qt::QueuedConnection);
    });
}

void RecorderController::reopenMicrophone() {
    if (!active_) return;
    const QString id = microphoneId_;
    const bool allowed = permission(PermissionKind::Microphone) == QLatin1String("granted");
    micStatus_ = id.isEmpty() ? QStringLiteral("Off") : (allowed ? QStringLiteral("Starting…") : QStringLiteral("No access"));
    emit statsChanged();
    worker_->post([this, id, allowed] {
        if (sources_.microphone) {
            sources_.microphone->stop();
            sources_.microphone.reset();
        }
        std::shared_ptr<LiveAudioSource> live;
        if (!id.isEmpty() && allowed) {
            services::RecordingRequest only;
            only.microphoneId = id == QLatin1String("default") ? "" : id.toStdString();
            if (auto set = services::openSources(backends_, only); set && set->microphone) live = set->microphone;
        }
        sources_.microphone = live;
        QMetaObject::invokeMethod(this, [this, live] { micLive_ = live; }, Qt::QueuedConnection);
    });
}

void RecorderController::reopenPhone() {
    if (!active_) return;
    const bool enabled = phoneCameraEnabled_;
    phoneStatus_ = enabled ? QStringLiteral("Starting…") : QStringLiteral("Idle");
    emit statsChanged();
    worker_->post([this, enabled] {
        // Stop and destroy the previous phone source, if any.
        if (sources_.phone) {
            sources_.phone->stop();
            sources_.phone.reset();
        }
        std::shared_ptr<capture::LiveVideoSource> live;
        std::string url;
        std::string error;
        if (enabled) {
            // Create a new PhoneCameraSource so the URL and token are fresh.
            auto phoneSrc = std::make_unique<mobile::PhoneCameraSource>();
            if (auto st = phoneSrc->listen(); !st) {
                error = st.error().message();
            } else {
                url = phoneSrc->url();
                if (auto lv = capture::LiveVideoSource::start(std::move(phoneSrc)); lv) {
                    live = std::move(*lv);
                } else {
                    error = lv.error().message();
                }
            }
        }
        sources_.phone = live;
        QMetaObject::invokeMethod(this, [this, live, url, error] {
            phoneLive_ = live;
            if (phonePreview_) phonePreview_->deleteLater();
            phonePreview_ = live ? new PreviewSource(live, this) : nullptr;
            emit previewChanged();
            phoneUrl_ = QString::fromStdString(url);
            emit phoneChanged();
            if (!error.empty()) {
                phoneStatus_ = QStringLiteral("Error");
                warnings_.append(QStringLiteral("Phone camera: ") + QString::fromStdString(error));
                emit warningsChanged();
            } else {
                phoneStatus_ = live ? QStringLiteral("Waiting for phone") : QStringLiteral("Idle");
            }
            emit statsChanged();
        }, Qt::QueuedConnection);
    });
}

void RecorderController::requestPermission(const QString& kind) {
    if (!backends_.permissions) return;
    backends_.permissions->request(permissionKindFrom(kind), [this, kind](PermissionStatus status) {
        QMetaObject::invokeMethod(this, [this, kind, status] {
            // ScreenCaptureKit reads the TCC decision only in a new process.
            // Relaunch immediately after an approved prompt instead of leaving
            // the user on a misleading "Allow" notice that cannot work yet.
            if (kind == QLatin1String("screen") && status == PermissionStatus::Granted) {
                restartApp();
                return;
            }
            // Otherwise macOS showed its dialog: the user switches Lectern on
            // in System Settings and then needs a restart (not another prompt).
            if (kind == QLatin1String("screen")) screenRestartNeeded_ = true;
            emit permissionsChanged();
            refreshSources();
        }, Qt::QueuedConnection);
    });
}

void RecorderController::recheckPermissions() {
    const QString before = cameraPermission() + microphonePermission() + screenPermission();
    emit permissionsChanged();
    if (before != cameraPermission() + microphonePermission() + screenPermission()) refreshSources();
}

void RecorderController::restartApp() {
#if defined(Q_OS_MACOS)
    // Relaunch the bundle once this process has exited (so its global
    // hotkeys and devices are free), through LaunchServices like a user would.
    const QString bundle = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
    QProcess::startDetached(QStringLiteral("/bin/sh"),
                            {QStringLiteral("-c"), QStringLiteral("sleep 0.6; /usr/bin/open \"$0\""), bundle});
#else
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
#endif
    QCoreApplication::quit();
}

void RecorderController::openPrivacySettings(const QString& kind) {
#if defined(Q_OS_MACOS)
    QString anchor = QStringLiteral("Privacy_ScreenCapture");
    if (kind == QLatin1String("camera")) anchor = QStringLiteral("Privacy_Camera");
    if (kind == QLatin1String("microphone")) anchor = QStringLiteral("Privacy_Microphone");
    QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?") + anchor));
#else
    (void)kind;
#endif
}

// ---------------------------------------------------------------------------
// Recording

void RecorderController::startRecording() {
    if (state_ != QLatin1String("idle")) return;
    if (screenTarget_.isEmpty() && cameraId_.isEmpty() && microphoneId_.isEmpty()) {
        setError(QStringLiteral("Choose at least one source to record."));
        return;
    }

    // Ask at the point of intent.  Previously the chooser only displayed a
    // permission notice, so pressing Record could fail with a vague source
    // error and, most importantly, never show the macOS Screen Recording
    // prompt.  Ask for one missing permission at a time; on success we enter
    // this method again and continue to the next required source.
    if (backends_.permissions) {
        auto requestMissing = [this](PermissionKind kind, const QString& label) {
            setState(QStringLiteral("preparing"));
            backends_.permissions->request(kind, [this, label](PermissionStatus status) {
                QMetaObject::invokeMethod(this, [this, label, status] {
                    emit permissionsChanged();
                    if (status == PermissionStatus::Granted || status == PermissionStatus::NotApplicable) {
                        setState(QStringLiteral("idle"));
                        refreshSources();
                        startRecording();
                    } else {
                        setState(QStringLiteral("idle"));
#if defined(Q_OS_WIN)
                        setError(label + QStringLiteral(" access is turned off. Enable it in Windows Settings > Privacy & security, then try again."));
#else
                        setError(label + QStringLiteral(" permission is required before recording. Enable it in System Settings, then try again."));
#endif
                    }
                }, Qt::QueuedConnection);
            });
        };
        const bool needsScreenPermission = !screenTarget_.isEmpty() || (systemAudio_ && systemAudioSupported_);
        if (needsScreenPermission && backends_.permissions->status(PermissionKind::ScreenCapture) != PermissionStatus::Granted) {
            requestMissing(PermissionKind::ScreenCapture, QStringLiteral("Screen Recording"));
            return;
        }
        if (!cameraId_.isEmpty() && backends_.permissions->status(PermissionKind::Camera) != PermissionStatus::Granted) {
            requestMissing(PermissionKind::Camera, QStringLiteral("Camera"));
            return;
        }
        if (!microphoneId_.isEmpty() && backends_.permissions->status(PermissionKind::Microphone) != PermissionStatus::Granted) {
            requestMissing(PermissionKind::Microphone, QStringLiteral("Microphone"));
            return;
        }
    }
    warnings_.clear();
    emit warningsChanged();
    setError({});
#if defined(Q_OS_WIN)
    // Windows captures every window on screen; keep ours (main window, HUD,
    // countdown) out of the recording. macOS excludes the app in its filter.
    if (settings_.recording.excludeOwnWindows) {
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            platform::excludeWindowFromCapture(static_cast<std::uintptr_t>(window->winId()));
        }
    }
#endif
    setState(QStringLiteral("preparing"));
    const auto request = buildRequest();
    const auto projectsRoot =
        settings_.projectsDirectory.empty() ? fs::defaultProjectsDirectory() : settings_.projectsDirectory;
    const std::string title =
        "Recording " + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm")).toStdString();

    worker_->post([this, request, projectsRoot, title] {
        // Screen and system audio are started now, during the countdown, so
        // they are warm at T0 (camera and microphone already run for preview).
        QStringList problems;
        if (request.screen && !sources_.screen) {
            auto s = services::openScreenSource(backends_, request);
            if (s) {
                sources_.screen = *s;
            } else {
                problems << QStringLiteral("Screen: ") + qs(s.error().message());
            }
        }
        if (request.systemAudio && !sources_.systemAudio) {
            services::RecordingRequest only;
            only.systemAudio = true;
            only.excludeOwnApplication = request.excludeOwnApplication;
            if (auto set = services::openSources(backends_, only); set && set->systemAudio) {
                sources_.systemAudio = set->systemAudio;
            } else {
                problems << QStringLiteral("System audio could not be captured");
            }
        }
        if (request.cameraId && !sources_.camera) problems << QStringLiteral("Camera is not available");
        if (request.microphoneId && !sources_.microphone) problems << QStringLiteral("Microphone is not available");
        // Phone camera is started by reopenPhone() when the user enables it;
        // if it's selected but not running (e.g. reopenPhone failed) warn the
        // user but continue – the recording can proceed without it.
        if (request.phone && !sources_.phone) problems << QStringLiteral("Phone camera is not available");

        Result<std::filesystem::path> folder = project::ProjectStore::createProjectFolder(projectsRoot, title);
        QString error;
        std::shared_ptr<RecordingSession> session;
        if (!folder) {
            error = qs(folder.error().message());
        } else {
            projectDir_ = *folder;
            SessionConfig cfg = services::makeSessionConfig(sources_, request, projectDir_, title);
            if (cfg.tracks.empty()) {
                error = QStringLiteral("No source could be opened. ") + problems.join(QStringLiteral("; "));
            } else if (auto created = RecordingSession::create(std::move(cfg), observer_.get())) {
                session = std::shared_ptr<RecordingSession>(std::move(*created));
            } else {
                error = qs(created.error().message());
            }
        }
        if (!error.isEmpty() && folder) {
            std::error_code ec;
            std::filesystem::remove_all(*folder, ec);
        }
        auto systemLive = sources_.systemAudio;
        auto screenLive = sources_.screen;
        QMetaObject::invokeMethod(this, [this, session, error, problems, systemLive, screenLive] {
            if (!problems.isEmpty()) {
                warnings_.append(problems);
                emit warningsChanged();
            }
            if (!session) {
                setState(QStringLiteral("idle"));
                setError(error);
                return;
            }
            sharedSession_ = session;
            systemLive_ = systemLive;
            screenLive_ = screenLive;
            if (countdownEnabled_ && settings_.recording.countdownSeconds > 0) {
                countdown_ = settings_.recording.countdownSeconds;
                emit countdownChanged();
                setState(QStringLiteral("countdown"));
                countdownTimer_.start();
            } else {
                beginRecordingNow();
            }
        }, Qt::QueuedConnection);
    });
}

void RecorderController::beginRecordingNow() {
    auto session = sharedSession_;
    if (!session) return;
    worker_->post([this, session] {
        auto st = session->start();
        QString error = st ? QString() : qs(st.error().message());
        QMetaObject::invokeMethod(this, [this, error] {
            if (!error.isEmpty()) {
                sharedSession_.reset();
                setState(QStringLiteral("idle"));
                setError(QStringLiteral("Recording could not start: ") + error);
                return;
            }
            setState(QStringLiteral("recording"));
        }, Qt::QueuedConnection);
    });
}

void RecorderController::togglePause() {
    if (!sharedSession_) return;
    if (state_ == QLatin1String("recording")) {
        if (sharedSession_->pause()) setState(QStringLiteral("paused"));
    } else if (state_ == QLatin1String("paused")) {
        if (sharedSession_->resume()) setState(QStringLiteral("recording"));
    }
}

void RecorderController::stopRecording() {
    if (state_ == QLatin1String("countdown")) {
        cancelRecording();
        return;
    }
    if (state_ != QLatin1String("recording") && state_ != QLatin1String("paused")) return;
    setState(QStringLiteral("stopping"));
    auto session = sharedSession_;
    worker_->post([session] { (void)session->stop(); });
}

void RecorderController::cancelRecording() {
    if (state_ == QLatin1String("idle") || state_ == QLatin1String("saving")) return;
    countdownTimer_.stop();
    auto session = sharedSession_;
    sharedSession_.reset();
    systemLive_.reset();
    screenLive_.reset();
    setState(QStringLiteral("cancelling"));
    worker_->post([this, session] {
        if (session) session->cancel();
        std::error_code ec;
        if (!projectDir_.empty()) std::filesystem::remove_all(projectDir_, ec);
        projectDir_.clear();
        if (sources_.screen) sources_.screen->stop();
        if (sources_.systemAudio) sources_.systemAudio->stop();
        sources_.screen.reset();
        sources_.systemAudio.reset();
        // Phone camera stays alive so the user can re-record without
        // re-scanning the QR code; the live source is not torn down here.
        QMetaObject::invokeMethod(this, [this] {
            setState(QStringLiteral("idle"));
            reopenScreen();
        }, Qt::QueuedConnection);
    });
}

void RecorderController::onSessionFinished(const SessionResult& result) {
    if (state_ == QLatin1String("cancelling") || state_ == QLatin1String("idle")) return;
    setState(QStringLiteral("saving"));
    auto session = sharedSession_;
    sharedSession_.reset();
    systemLive_.reset();
    screenLive_.reset();
    worker_->post([this, result, session]() mutable {
        if (sources_.screen) sources_.screen->stop();
        if (sources_.systemAudio) sources_.systemAudio->stop();
        sources_.screen.reset();
        sources_.systemAudio.reset();
        // Camera, microphone and phone stay alive so previews and levels work
        // while the editor opens and the user sets up the next recording.
        session.reset();
        QString error;
        QString dir = qs(result.projectDir.string());
        if (result.state != SessionState::Completed) {
            error = QStringLiteral("The recording failed: no track could be written.");
        } else if (auto proj = services::importRecording(result.projectDir, result.manifest); !proj) {
            error = qs(proj.error().message());
        }
        QMetaObject::invokeMethod(this, [this, error, dir] {
            setState(QStringLiteral("idle"));
            elapsedText_ = QStringLiteral("0:00");
            droppedFrames_ = 0;
            diskWarning_.clear();
            presenterOverlay_ = false;
            emit statsChanged();
            if (!error.isEmpty()) {
                setError(error);
                return;
            }
            emit recordingFinished(dir);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------

void RecorderController::onTick() {
    // Levels: live sources work before and during recording.
    const qreal mic = micLive_ ? meterLevel(micLive_->levels().maxPeakDb()) : 0.0;
    const qreal sys = systemLive_ ? meterLevel(systemLive_->levels().maxPeakDb()) : 0.0;
    const bool clip = micLive_ && micLive_->levels().channel[0].clipped;
    if (std::fabs(mic - micLevel_) > 0.005 || std::fabs(sys - systemLevel_) > 0.005 || clip != micClipping_) {
        micLevel_ = mic;
        systemLevel_ = sys;
        micClipping_ = clip;
        emit levelsChanged();
    }

    if (++tickCount_ % 2) return;  // stats at 10 Hz

    const std::int64_t now = HostClock::now();
    QString screenS = screenStatus_;
    if (screenLive_) {
        const auto st = screenLive_->stats();
        screenS = st.framesReceived == 0                         ? QStringLiteral("Starting…")
                  : now - st.lastFrameHostNs < 1'000'000'000LL ? QStringLiteral("Live")
                                                               : QStringLiteral("No signal");
    } else if (screenTarget_.isEmpty()) {
        screenS = QStringLiteral("Off");
    }
    QString cam = cameraStatus_;
    if (cameraLive_) {
        const auto st = cameraLive_->stats();
        cam = st.framesReceived == 0                         ? QStringLiteral("Starting…")
              : now - st.lastFrameHostNs < 1'000'000'000LL ? QStringLiteral("Live")
                                                           : QStringLiteral("No signal");
    }
    QString micS = micStatus_;
    if (micLive_) {
        const auto st = micLive_->stats();
        micS = st.lastChunkHostNs != 0 && now - st.lastChunkHostNs < 1'000'000'000LL ? QStringLiteral("Live")
                                                                                     : QStringLiteral("No input");
    }
    // Phone camera: derive status from the LiveVideoSource stats.
    QString phoneS = phoneStatus_;
    quint64 phoneFr = phoneFramesReceived_;
    if (phoneLive_) {
        const auto st = phoneLive_->stats();
        phoneFr = st.framesReceived;
        phoneS = st.framesReceived == 0                        ? QStringLiteral("Waiting for phone")
                 : now - st.lastFrameHostNs < 2'000'000'000LL ? QStringLiteral("Streaming")
                                                              : QStringLiteral("No signal");
    }
    bool phoneDataChanged = phoneFr != phoneFramesReceived_;
    bool changed = screenS != screenStatus_ || cam != cameraStatus_ || micS != micStatus_ || phoneS != phoneStatus_;
    screenStatus_ = screenS;
    cameraStatus_ = cam;
    micStatus_ = micS;
    phoneStatus_ = phoneS;
    phoneFramesReceived_ = phoneFr;
    if (phoneDataChanged) emit phoneChanged();

    if (sharedSession_ && (state_ == QLatin1String("recording") || state_ == QLatin1String("paused") ||
                           state_ == QLatin1String("stopping"))) {
        const RecordingStats s = sharedSession_->stats();
        const QString elapsed = qs(formatDuration(s.duration));
        const int dropped = static_cast<int>(s.droppedFrames());
        QString disk;
        if (s.diskLevel != DiskLevel::Ok) {
            const int minutes = s.diskSecondsRemaining > 0 ? static_cast<int>(s.diskSecondsRemaining / 60) : -1;
            disk = minutes >= 0 ? QStringLiteral("Low disk space · about %1 min left").arg(minutes)
                                : QStringLiteral("Low disk space");
        }
        const bool overlay = screenLive_ && screenLive_->videoEffectActive();
        changed |= elapsed != elapsedText_ || dropped != droppedFrames_ || disk != diskWarning_ || overlay != presenterOverlay_;
        elapsedText_ = elapsed;
        droppedFrames_ = dropped;
        diskWarning_ = disk;
        presenterOverlay_ = overlay;
    }
    if (changed) emit statsChanged();
}

}  // namespace lectern::ui
