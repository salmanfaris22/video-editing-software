#pragma once

#include "capture/CaptureInterfaces.h"
#include "capture/RecordingSession.h"
#include "core/Settings.h"
#include "services/RecordingSetup.h"
#include "ui/SerialExecutor.h"
#include "ui/VideoPreviewItem.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

namespace lectern::ui {

/// View model of the recording screen and HUD. It owns no business state of
/// its own: device lists come from the capture backends, recording state
/// from the RecordingSession. Everything that can block runs on `worker_`.
class RecorderController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by AppController")

    Q_PROPERTY(QVariantList screenTargets READ screenTargets NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList microphones READ microphones NOTIFY sourcesChanged)
    Q_PROPERTY(bool systemAudioSupported READ systemAudioSupported NOTIFY sourcesChanged)
    Q_PROPERTY(bool loadingSources READ loadingSources NOTIFY loadingSourcesChanged)

    Q_PROPERTY(QString screenTarget READ screenTarget WRITE setScreenTarget NOTIFY selectionChanged)
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId NOTIFY selectionChanged)
    Q_PROPERTY(QString microphoneId READ microphoneId WRITE setMicrophoneId NOTIFY selectionChanged)
    Q_PROPERTY(bool systemAudio READ systemAudio WRITE setSystemAudio NOTIFY selectionChanged)
    Q_PROPERTY(QString resolution READ resolution WRITE setResolution NOTIFY selectionChanged)
    Q_PROPERTY(int frameRate READ frameRate WRITE setFrameRate NOTIFY selectionChanged)
    Q_PROPERTY(bool countdownEnabled READ countdownEnabled WRITE setCountdownEnabled NOTIFY selectionChanged)

    Q_PROPERTY(QString screenPermission READ screenPermission NOTIFY permissionsChanged)
    Q_PROPERTY(QString cameraPermission READ cameraPermission NOTIFY permissionsChanged)
    Q_PROPERTY(QString microphonePermission READ microphonePermission NOTIFY permissionsChanged)
    /// Screen Recording was requested: macOS applies it only to a new process,
    /// so the app must restart after the user switches it on in Settings.
    Q_PROPERTY(bool screenRestartNeeded READ screenRestartNeeded NOTIFY permissionsChanged)

    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(int countdown READ countdown NOTIFY countdownChanged)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY statsChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY statsChanged)
    Q_PROPERTY(QString diskWarning READ diskWarning NOTIFY statsChanged)
    /// macOS Presenter Overlay is drawing the camera into the screen recording.
    Q_PROPERTY(bool presenterOverlay READ presenterOverlay NOTIFY statsChanged)
    Q_PROPERTY(QString screenStatus READ screenStatus NOTIFY statsChanged)
    Q_PROPERTY(QString cameraStatus READ cameraStatus NOTIFY statsChanged)
    Q_PROPERTY(QString micStatus READ micStatus NOTIFY statsChanged)
    /// Status text for the phone camera ("Idle" | "Waiting for phone" | "Streaming" | "Error").
    Q_PROPERTY(QString phoneStatus READ phoneStatus NOTIFY statsChanged)
    Q_PROPERTY(qreal micLevel READ micLevel NOTIFY levelsChanged)
    Q_PROPERTY(qreal systemLevel READ systemLevel NOTIFY levelsChanged)
    Q_PROPERTY(bool micClipping READ micClipping NOTIFY levelsChanged)
    Q_PROPERTY(lectern::ui::PreviewSource* screenPreview READ screenPreview NOTIFY previewChanged)
    Q_PROPERTY(lectern::ui::PreviewSource* cameraPreview READ cameraPreview NOTIFY previewChanged)
    Q_PROPERTY(lectern::ui::PreviewSource* phonePreview READ phonePreview NOTIFY previewChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY warningsChanged)
    /// Whether the phone camera source should be included in the recording.
    Q_PROPERTY(bool phoneCameraEnabled READ phoneCameraEnabled WRITE setPhoneCameraEnabled NOTIFY selectionChanged)
    /// The pairing URL to display as a QR code / link (non-empty when phone source is started).
    Q_PROPERTY(QString phoneUrl READ phoneUrl NOTIFY phoneChanged)
    /// Number of frames the phone has sent this session (0 = no phone connected yet).
    Q_PROPERTY(quint64 phoneFramesReceived READ phoneFramesReceived NOTIFY phoneChanged)

public:
    RecorderController(capture::CaptureBackends& backends, AppSettings& settings,
                       std::function<void()> settingsChanged, QObject* parent = nullptr);
    ~RecorderController() override;

    // Recording screen lifecycle.
    Q_INVOKABLE void activate();
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE void refreshSources();
    Q_INVOKABLE void requestPermission(const QString& kind);
    /// Re-reads permission states (e.g. when the app becomes active again).
    Q_INVOKABLE void recheckPermissions();
    /// Quits and starts Lectern again (Screen Recording takes effect).
    Q_INVOKABLE void restartApp();
    Q_INVOKABLE void openPrivacySettings(const QString& kind);

    // Recording control (also bound to keyboard shortcuts).
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void cancelRecording();
    Q_INVOKABLE void dismissError();
    /// Copies the phone pairing URL to the system clipboard.
    Q_INVOKABLE void copyPhoneUrl();

    [[nodiscard]] QVariantList screenTargets() const { return screenTargets_; }
    [[nodiscard]] QVariantList cameras() const { return cameras_; }
    [[nodiscard]] QVariantList microphones() const { return microphones_; }
    [[nodiscard]] bool systemAudioSupported() const { return systemAudioSupported_; }
    [[nodiscard]] bool loadingSources() const { return loadingSources_; }
    [[nodiscard]] QString screenTarget() const { return screenTarget_; }
    void setScreenTarget(const QString& v);
    [[nodiscard]] QString cameraId() const { return cameraId_; }
    void setCameraId(const QString& v);
    [[nodiscard]] QString microphoneId() const { return microphoneId_; }
    void setMicrophoneId(const QString& v);
    [[nodiscard]] bool systemAudio() const { return systemAudio_; }
    void setSystemAudio(bool v);
    [[nodiscard]] QString resolution() const { return resolution_; }
    void setResolution(const QString& v);
    [[nodiscard]] int frameRate() const { return frameRate_; }
    void setFrameRate(int v);
    [[nodiscard]] bool countdownEnabled() const { return countdownEnabled_; }
    void setCountdownEnabled(bool v);
    [[nodiscard]] QString screenPermission() const { return permission(capture::PermissionKind::ScreenCapture); }
    [[nodiscard]] QString cameraPermission() const { return permission(capture::PermissionKind::Camera); }
    [[nodiscard]] QString microphonePermission() const { return permission(capture::PermissionKind::Microphone); }
    [[nodiscard]] bool screenRestartNeeded() const {
        return screenRestartNeeded_ && permission(capture::PermissionKind::ScreenCapture) != QLatin1String("granted");
    }
    [[nodiscard]] QString state() const { return state_; }
    [[nodiscard]] int countdown() const { return countdown_; }
    [[nodiscard]] QString elapsedText() const { return elapsedText_; }
    [[nodiscard]] int droppedFrames() const { return droppedFrames_; }
    [[nodiscard]] QString diskWarning() const { return diskWarning_; }
    [[nodiscard]] bool presenterOverlay() const { return presenterOverlay_; }
    [[nodiscard]] QString screenStatus() const { return screenStatus_; }
    [[nodiscard]] QString cameraStatus() const { return cameraStatus_; }
    [[nodiscard]] QString micStatus() const { return micStatus_; }
    [[nodiscard]] QString phoneStatus() const { return phoneStatus_; }
    [[nodiscard]] qreal micLevel() const { return micLevel_; }
    [[nodiscard]] qreal systemLevel() const { return systemLevel_; }
    [[nodiscard]] bool micClipping() const { return micClipping_; }
    [[nodiscard]] PreviewSource* screenPreview() const { return screenPreview_; }
    [[nodiscard]] PreviewSource* cameraPreview() const { return cameraPreview_; }
    [[nodiscard]] PreviewSource* phonePreview() const { return phonePreview_; }
    [[nodiscard]] QString errorMessage() const { return errorMessage_; }
    [[nodiscard]] QStringList warnings() const { return warnings_; }
    [[nodiscard]] bool phoneCameraEnabled() const { return phoneCameraEnabled_; }
    void setPhoneCameraEnabled(bool v);
    [[nodiscard]] QString phoneUrl() const { return phoneUrl_; }
    [[nodiscard]] quint64 phoneFramesReceived() const { return phoneFramesReceived_; }

signals:
    void sourcesChanged();
    void loadingSourcesChanged();
    void selectionChanged();
    void permissionsChanged();
    void stateChanged();
    void countdownChanged();
    void statsChanged();
    void levelsChanged();
    void previewChanged();
    void errorChanged();
    void warningsChanged();
    /// Emitted when phoneUrl or phoneFramesReceived changes.
    void phoneChanged();
    /// Emitted when the recording is saved as a project and the editor can open it.
    void recordingFinished(const QString& projectDir);

private:
    class Observer;

    [[nodiscard]] QString permission(capture::PermissionKind kind) const;
    [[nodiscard]] services::RecordingRequest buildRequest() const;
    void setState(const QString& s);
    void setError(const QString& message);
    void persistSelection();
    void reopenScreen();
    void reopenCamera();
    void reopenMicrophone();
    void reopenPhone();
    void onTick();
    void beginRecordingNow();
    void onSessionFinished(const capture::SessionResult& result);

    capture::CaptureBackends& backends_;
    AppSettings& settings_;
    std::function<void()> settingsChanged_;
    std::unique_ptr<SerialExecutor> worker_;
    std::unique_ptr<Observer> observer_;

    // Worker-thread state: running sources and the current project folder.
    services::LiveSourceSet sources_;
    std::filesystem::path projectDir_;

    // UI-thread state. The session is shared with worker tasks (start/stop);
    // whichever side drops the last reference destroys it after it finished.
    std::shared_ptr<capture::RecordingSession> sharedSession_;
    std::shared_ptr<capture::LiveAudioSource> micLive_;
    std::shared_ptr<capture::LiveAudioSource> systemLive_;
    std::shared_ptr<capture::LiveVideoSource> screenLive_;
    std::shared_ptr<capture::LiveVideoSource> cameraLive_;
    std::shared_ptr<capture::LiveVideoSource> phoneLive_;
    int tickCount_ = 0;

    QVariantList screenTargets_;
    QVariantList cameras_;
    QVariantList microphones_;
    bool systemAudioSupported_ = false;
    bool loadingSources_ = false;
    bool active_ = false;

    QString screenTarget_;
    QString cameraId_;
    QString microphoneId_;
    bool systemAudio_ = true;
    bool phoneCameraEnabled_ = false;
    QString resolution_ = QStringLiteral("1080p");
    int frameRate_ = 30;
    bool countdownEnabled_ = true;

    bool screenRestartNeeded_ = false;
    QString state_ = QStringLiteral("idle");
    int countdown_ = 0;
    QString elapsedText_ = QStringLiteral("0:00");
    int droppedFrames_ = 0;
    QString diskWarning_;
    bool presenterOverlay_ = false;
    QString screenStatus_ = QStringLiteral("Off");
    QString cameraStatus_ = QStringLiteral("Off");
    QString micStatus_ = QStringLiteral("Off");
    QString phoneStatus_ = QStringLiteral("Idle");
    qreal micLevel_ = 0;
    qreal systemLevel_ = 0;
    bool micClipping_ = false;
    PreviewSource* screenPreview_ = nullptr;
    PreviewSource* cameraPreview_ = nullptr;
    PreviewSource* phonePreview_ = nullptr;
    QString phoneUrl_;
    quint64 phoneFramesReceived_ = 0;
    QString errorMessage_;
    QStringList warnings_;

    QTimer tick_;
    QTimer countdownTimer_;
};

}  // namespace lectern::ui
