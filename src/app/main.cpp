// Application entry point: logging, FFmpeg, platform setup, Qt Quick.

#include "core/FileSystem.h"
#include "core/Log.h"
#include "editor/PlatformSegmenter.h"
#ifdef LECTERN_HAS_GPU_RENDERER
#include "render/GpuRenderer.h"
#endif
#include "media/FFmpeg.h"
#include "platform/PlatformBackends.h"
#include "ui/AppController.h"
#include "ui/IconProvider.h"
#include "ui/LookPreviewProvider.h"
#include "ui/ThumbnailProvider.h"
#include "ui/WaveformProvider.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QSettings>
#include <QQmlApplicationEngine>
#include <QQmlEngineExtensionPlugin>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

Q_IMPORT_QML_PLUGIN(Lectern_UIPlugin)

using namespace lectern;

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral(LECTERN_PRODUCT_NAME));
    QGuiApplication::setOrganizationName(QStringLiteral(LECTERN_PRODUCT_NAME));
    // Tests and automated runs keep their workspace (window state, theme …) out of the user's preferences.
    if (const QByteArray dir = qgetenv("LECTERN_APP_DATA_DIR"); !dir.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromLocal8Bit(dir));
    }
    QGuiApplication::setApplicationVersion(QStringLiteral(LECTERN_VERSION));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Screen recorder and editor"));
    cli.addHelpOption();
    cli.addVersionOption();
    const QCommandLineOption synthetic(QStringLiteral("synthetic"), QStringLiteral("Use synthetic capture sources."));
    const QCommandLineOption view(QStringLiteral("view"), QStringLiteral("Initial view: home|record|editor."),
                                  QStringLiteral("view"), QStringLiteral("home"));
    const QCommandLineOption project(QStringLiteral("project"), QStringLiteral("Open a project folder."),
                                     QStringLiteral("dir"));
    const QCommandLineOption screenshot(QStringLiteral("screenshot"),
                                        QStringLiteral("Render the window to a PNG and quit (testing)."),
                                        QStringLiteral("file"));
    const QCommandLineOption delay(QStringLiteral("screenshot-delay"), QStringLiteral("Delay before the screenshot (ms)."),
                                   QStringLiteral("ms"), QStringLiteral("2500"));
    const QCommandLineOption logLevel(QStringLiteral("log-level"), QStringLiteral("trace|debug|info|warn|error"),
                                      QStringLiteral("level"), QStringLiteral("info"));
    const QCommandLineOption autoRecord(QStringLiteral("auto-record"),
                                        QStringLiteral("Testing: drive the UI flow — record N seconds, stop, open the editor."),
                                        QStringLiteral("seconds"));
    const QCommandLineOption editorTool(QStringLiteral("editor-tool"),
                                        QStringLiteral("Editor panel to show: setup|layout|cut|effects|overlay|style|subtitles|audio|adjust."),
                                        QStringLiteral("tool"));
    const QCommandLineOption editorPage(QStringLiteral("editor-page"), QStringLiteral("Editor page: edit|color."),
                                        QStringLiteral("page"));
    cli.addOptions({synthetic, view, project, screenshot, delay, logLevel, autoRecord, editorTool, editorPage});
    cli.process(app);

    // Logging: stderr + rotating file (docs/ARCHITECTURE.md §7).
    Logger::instance().addSink(makeStderrSink());
    Logger::instance().addSink(makeRotatingFileSink(fs::logDirectory() / "lectern.log"));
    // Qt and QML messages (warnings, console.log/info) go to the same log.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& message) {
        const LogLevel level = type == QtDebugMsg ? LogLevel::Debug
                               : type == QtInfoMsg ? LogLevel::Info
                               : type == QtWarningMsg ? LogLevel::Warn
                                                      : LogLevel::Error;
        const char* category = context.category && std::string_view(context.category) != "default" ? context.category
                               : context.file && std::string_view(context.file).ends_with(".qml")    ? "qml"
                                                                                                    : "qt";
        LEC_LOG(level, category, "{}", message.toStdString());
    });
    Logger::instance().setLevel(logLevelFromString(cli.value(logLevel).toStdString()));
    LEC_INFO("app", "{} {} starting", LECTERN_PRODUCT_NAME, LECTERN_VERSION);

    media::initializeFFmpeg(LogLevel::Warn);
    platform::initializePlatform();
    editor::installPlatformSegmenter();  // camera background blur
#ifdef LECTERN_HAS_GPU_RENDERER
    render::installGpuRenderer();  // preview and export on the GPU (LECTERN_RENDERER=cpu to opt out)
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ui::AppController controller(ui::AppController::Options{cli.isSet(synthetic)});
    if (cli.isSet(editorTool)) controller.setWorkspaceValue(QStringLiteral("editorTool"), cli.value(editorTool));
    if (cli.isSet(editorPage)) controller.setWorkspaceValue(QStringLiteral("editorPage"), cli.value(editorPage));
    if (cli.isSet(project)) controller.openProject(cli.value(project));
    else controller.setView(cli.value(view));

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"), new ui::ThumbnailProvider());
    engine.addImageProvider(QStringLiteral("icon"), new ui::IconProvider());
    engine.addImageProvider(QStringLiteral("waveform"), new ui::WaveformProvider());
    engine.addImageProvider(QStringLiteral("look"), new ui::LookPreviewProvider());
    engine.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&controller)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("Lectern.UI", "Main");
    if (engine.rootObjects().isEmpty()) return 1;

    if (cli.isSet(autoRecord)) {
        // Exercises the real controllers end to end: record view → start (with
        // countdown) → record → stop → save project → editor.
        const int seconds = cli.value(autoRecord).toInt();
        controller.setView(QStringLiteral("record"));
        auto* recorder = controller.recorder();
        QTimer::singleShot(1500, recorder, [recorder] { recorder->startRecording(); });
        QObject::connect(recorder, &ui::RecorderController::stateChanged, recorder, [recorder, seconds] {
            LEC_INFO("app", "auto-record: state {}", recorder->state().toStdString());
            if (recorder->state() == QLatin1String("recording")) {
                QTimer::singleShot(seconds * 1000, recorder, [recorder] { recorder->stopRecording(); });
            }
        });
        QObject::connect(&controller, &ui::AppController::viewChanged, &app, [&controller, &cli, &screenshot, &engine] {
            if (controller.view() != QLatin1String("editor")) return;
            LEC_INFO("app", "auto-record: editor opened with '{}'", controller.project()->title().toStdString());
            if (!cli.isSet(screenshot)) QTimer::singleShot(500, [] { QCoreApplication::exit(0); });
            (void)engine;
        });
    }

    if (cli.isSet(screenshot) && cli.isSet(autoRecord)) {
        const QString file = cli.value(screenshot);
        QObject::connect(&controller, &ui::AppController::viewChanged, &app, [&controller, &engine, file] {
            if (controller.view() != QLatin1String("editor")) return;
            QTimer::singleShot(2500, [&engine, file] {
                auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
                const bool saved = window && window->grabWindow().save(file);
                LEC_INFO("app", "screenshot {} {}", file.toStdString(), saved ? "saved" : "FAILED");
                QCoreApplication::exit(saved ? 0 : 1);
            });
        });
    } else if (cli.isSet(screenshot)) {
        const QString file = cli.value(screenshot);
        QTimer::singleShot(cli.value(delay).toInt(), &app, [&engine, file] {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
            const QImage image = window ? window->grabWindow() : QImage();
            const bool saved = !image.isNull() && image.save(file);
            LEC_INFO("app", "screenshot {} {}", file.toStdString(), saved ? "saved" : "FAILED");
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }

    const int rc = QGuiApplication::exec();
    LEC_INFO("app", "exiting ({})", rc);
    Logger::instance().flush();
    return rc;
}
