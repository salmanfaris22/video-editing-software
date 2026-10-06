#pragma once

#include "capture/CaptureInterfaces.h"
#include "core/Settings.h"
#include "platform/GlobalHotkeys.h"
#include "ui/ExportController.h"
#include "ui/McpController.h"
#include "ui/PlaybackController.h"
#include "ui/ProjectController.h"
#include "ui/RecorderController.h"
#include "ui/SerialExecutor.h"

#include <QObject>
#include <QSettings>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace lectern::ui {

/// Root view model: navigation between Home, Record and Editor, settings
/// persistence, startup crash recovery, recent projects and workspace state.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by main()")

    Q_PROPERTY(QString view READ view NOTIFY viewChanged)
    Q_PROPERTY(lectern::ui::RecorderController* recorder READ recorder CONSTANT)
    Q_PROPERTY(lectern::ui::ProjectController* project READ project CONSTANT)
    Q_PROPERTY(lectern::ui::PlaybackController* playback READ playback CONSTANT)
    Q_PROPERTY(lectern::ui::ExportController* exporter READ exporter CONSTANT)
    Q_PROPERTY(lectern::ui::McpController* assistants READ assistants CONSTANT)
    Q_PROPERTY(QString productName READ productName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(bool syntheticSources READ syntheticSources CONSTANT)
    Q_PROPERTY(bool globalHotkeys READ globalHotkeys CONSTANT)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
    Q_PROPERTY(QString banner READ banner NOTIFY bannerChanged)
    Q_PROPERTY(QString bannerProject READ bannerProject NOTIFY bannerChanged)

public:
    struct Options {
        bool synthetic = false;
    };

    explicit AppController(Options options, QObject* parent = nullptr);
    ~AppController() override;

    Q_INVOKABLE void goHome();
    /// ⌘⇧R: open the recording screen / start / stop (one entry point for the
    /// in-app shortcut and the system-wide hotkey).
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void newRecording();
    Q_INVOKABLE void openProject(const QString& dir);
    Q_INVOKABLE void openProjectUrl(const QUrl& url);
    Q_INVOKABLE void dismissBanner();
    Q_INVOKABLE void revealInFinder(const QString& path);
    Q_INVOKABLE QVariant workspaceValue(const QString& key, const QVariant& fallback = {}) const;
    Q_INVOKABLE void setWorkspaceValue(const QString& key, const QVariant& value);

    [[nodiscard]] QString view() const { return view_; }
    [[nodiscard]] RecorderController* recorder() const { return recorder_; }
    [[nodiscard]] ProjectController* project() const { return project_; }
    [[nodiscard]] PlaybackController* playback() const { return playback_; }
    [[nodiscard]] ExportController* exporter() const { return exporter_; }
    [[nodiscard]] McpController* assistants() const { return assistants_; }
    [[nodiscard]] QString productName() const;
    [[nodiscard]] QString version() const;
    [[nodiscard]] QString platformName() const { return platformName_; }
    [[nodiscard]] bool syntheticSources() const { return options_.synthetic; }
    [[nodiscard]] bool globalHotkeys() const { return globalHotkeys_; }
    [[nodiscard]] QVariantList recentProjects() const;
    [[nodiscard]] QString banner() const { return banner_; }
    [[nodiscard]] QString bannerProject() const { return bannerProject_; }

    /// For --view on the command line (screenshots / automation).
    void setView(const QString& view);

signals:
    void viewChanged();
    void recentProjectsChanged();
    void bannerChanged();
    /// The window should come to the front (e.g. hotkey pressed in another app).
    void activateRequested();

private:
    void saveSettings();
    void addRecent(const QString& dir);
    void checkForInterruptedRecordings();
    void registerGlobalHotkeys();

    Options options_;
    AppSettings settings_;
    capture::CaptureBackends backends_;
    std::unique_ptr<SerialExecutor> worker_;
    RecorderController* recorder_ = nullptr;
    ProjectController* project_ = nullptr;
    PlaybackController* playback_ = nullptr;
    ExportController* exporter_ = nullptr;
    McpController* assistants_ = nullptr;
    mutable QSettings workspace_;
    QString view_ = QStringLiteral("home");
    QString platformName_;
    QString banner_;
    QString bannerProject_;
    std::unique_ptr<platform::IGlobalHotkeys> hotkeys_;
    bool globalHotkeys_ = false;
};

}  // namespace lectern::ui
