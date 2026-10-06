// lectern-rec: headless recorder on top of the recording engine.
//
// Proves the engine runs without any UI (docs/ARCHITECTURE.md §3), and is the
// tool for real-device smoke tests, benchmarks and crash-recovery drills.

#include "capture/RecoveryService.h"
#include "capture/synthetic/SyntheticSources.h"
#include "core/FileSystem.h"
#include "core/Log.h"
#include "core/Uuid.h"
#include "media/FFmpeg.h"
#include "media/HardwareCapabilities.h"
#include "platform/PlatformBackends.h"
#include "project/ProjectStore.h"
#include "services/RecordingImporter.h"
#include "services/RecordingSetup.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace lectern;
using namespace lectern::capture;

namespace {

std::atomic<bool> gStopRequested{false};
void onSignal(int) { gStopRequested.store(true); }

void usage() {
    std::printf(R"(lectern-rec — headless recorder (%s %s)

usage:
  lectern-rec --list
  lectern-rec [recording options] [--out <project-dir>]
  lectern-rec --recover [--registry <dir>]
  lectern-rec --recover-session <project-dir> <session-dir>
  lectern-rec --request-permission screen|camera|microphone

recording options:
  --display <id|main>     record a display (default when no other screen target is given)
  --window <id>           record one window
  --app <bundle-id>       record an application's windows
  --no-screen             no screen track
  --camera <id|default>   add a camera track
  --mic <id|default>      add a microphone track
  --system-audio          add a system audio track
  --resolution <p>        720p | 1080p | 1440p | 2160p | native   (default 1080p)
  --fps <n>               24 | 30 | 60                            (default 30)
  --quality <q>           standard | high                         (default high)
  --encoder <e>           auto | hardware | software              (default auto)
  --duration <seconds>    stop automatically (default: until Ctrl+C)
  --pause-at <s> --resume-at <s>   pause/resume at these times (testing)
  --synthetic             synthetic sources instead of real devices
  --title <text>          recording title
  --no-project            keep only the media + manifest (no project.json)
  --stats                 print live statistics every second
  --log-level <level>     trace | debug | info | warn | error
)",
                LECTERN_PRODUCT_NAME, LECTERN_VERSION);
}

struct Options {
    bool list = false;
    bool recover = false;
    std::string recoverProject;
    std::string recoverSession;
    std::string requestPermission;
    std::string registry;
    services::RecordingRequest request;
    bool noScreen = false;
    bool synthetic = false;
    bool project = true;
    bool stats = false;
    double duration = 0;
    double pauseAt = -1;
    double resumeAt = -1;
    std::string out;
    std::string title;
    LogLevel logLevel = LogLevel::Info;
};

bool parse(int argc, char** argv, Options& o) {
    auto need = [&](int& i) -> const char* {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %s\n", argv[i]);
            std::exit(2);
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            usage();
            std::exit(0);
        } else if (a == "--list") {
            o.list = true;
        } else if (a == "--recover") {
            o.recover = true;
        } else if (a == "--recover-session") {
            o.recoverProject = need(i);
            o.recoverSession = need(i);
        } else if (a == "--registry") {
            o.registry = need(i);
        } else if (a == "--request-permission") {
            o.requestPermission = need(i);
        } else if (a == "--display") {
            o.request.screen = ScreenCaptureTarget{ScreenTargetKind::Display, need(i), {}};
        } else if (a == "--window") {
            o.request.screen = ScreenCaptureTarget{ScreenTargetKind::Window, need(i), {}};
        } else if (a == "--app") {
            o.request.screen = ScreenCaptureTarget{ScreenTargetKind::Application, need(i), "main"};
        } else if (a == "--no-screen") {
            o.noScreen = true;
        } else if (a == "--camera") {
            const std::string v = need(i);
            o.request.cameraId = v == "default" ? "" : v;
        } else if (a == "--mic") {
            const std::string v = need(i);
            o.request.microphoneId = v == "default" ? "" : v;
        } else if (a == "--system-audio") {
            o.request.systemAudio = true;
        } else if (a == "--resolution") {
            o.request.resolution = need(i);
        } else if (a == "--fps") {
            o.request.frameRate = std::atoi(need(i));
        } else if (a == "--quality") {
            o.request.quality = need(i);
        } else if (a == "--encoder") {
            o.request.encoder = need(i);
        } else if (a == "--duration") {
            o.duration = std::atof(need(i));
        } else if (a == "--pause-at") {
            o.pauseAt = std::atof(need(i));
        } else if (a == "--resume-at") {
            o.resumeAt = std::atof(need(i));
        } else if (a == "--synthetic") {
            o.synthetic = true;
        } else if (a == "--title") {
            o.title = need(i);
        } else if (a == "--out") {
            o.out = need(i);
        } else if (a == "--no-project") {
            o.project = false;
        } else if (a == "--stats") {
            o.stats = true;
        } else if (a == "--log-level") {
            o.logLevel = logLevelFromString(need(i));
        } else {
            std::fprintf(stderr, "unknown option %s (see --help)\n", a.c_str());
            return false;
        }
    }
    if (!o.noScreen && !o.request.screen) o.request.screen = ScreenCaptureTarget{ScreenTargetKind::Display, "main", {}};
    if (o.noScreen) o.request.screen.reset();
    return true;
}

int listDevices(CaptureBackends& b) {
    const auto info = platform::platformInfo();
    std::printf("Platform: %s\n\nPermissions:\n", info.os.c_str());
    for (auto k : {PermissionKind::ScreenCapture, PermissionKind::Camera, PermissionKind::Microphone}) {
        std::printf("  %-12s %s\n", std::string(toString(k)).c_str(), std::string(toString(b.permissions->status(k))).c_str());
    }
    std::printf("\nScreens (%s):\n", b.screen->name().c_str());
    if (auto t = b.screen->enumerateTargets()) {
        for (const auto& d : t->displays) {
            std::printf("  display %-12s %s %dx%d @ %.0f Hz%s\n", d.id.c_str(), d.name.c_str(), d.widthPx, d.heightPx,
                        d.refreshRate, d.isMain ? " (main)" : "");
        }
        for (const auto& w : t->windows) {
            std::printf("  window  %-12s %s — %s\n", w.id.c_str(), w.applicationName.c_str(), w.title.c_str());
        }
        for (const auto& a : t->applications) std::printf("  app     %-40s %s\n", a.id.c_str(), a.name.c_str());
    } else {
        std::printf("  unavailable: %s\n", t.error().message().c_str());
    }
    std::printf("\nCameras (%s):\n", b.camera->name().c_str());
    if (auto c = b.camera->enumerateCameras()) {
        for (const auto& cam : *c) {
            int maxW = 0;
            int maxH = 0;
            for (const auto& f : cam.formats) {
                if (f.width * f.height > maxW * maxH) {
                    maxW = f.width;
                    maxH = f.height;
                }
            }
            std::printf("  %s  %s (up to %dx%d)%s\n", cam.id.c_str(), cam.name.c_str(), maxW, maxH,
                        cam.continuity ? " [Continuity]" : "");
        }
    }
    std::printf("\nMicrophones (%s):\n", b.audio->name().c_str());
    if (auto m = b.audio->enumerateInputs()) {
        for (const auto& mic : *m) {
            std::printf("  %s  %s, %d ch @ %d Hz, %s%s\n", mic.id.c_str(), mic.name.c_str(), mic.channels,
                        mic.sampleRate, mic.transport.c_str(), mic.isDefault ? " (default)" : "");
        }
    }
    std::printf("\nSystem audio: %s\n", b.audio->supportsSystemAudio() ? "supported" : "not supported");
    std::printf("\nVideo encoders:\n%s\n", media::HardwareCapabilities::probe(true).describe().c_str());
    return 0;
}

void printStats(const RecordingStats& s) {
    std::printf("[%s] %s", formatDuration(s.duration).c_str(), std::string(toString(s.state)).c_str());
    for (const auto& t : s.tracks) {
        if (t.video) {
            std::printf(" | %s %llu fr (dup %llu, drop %llu) %s%s", t.id.c_str(),
                        static_cast<unsigned long long>(t.video->framesEncoded),
                        static_cast<unsigned long long>(t.video->framesDuplicated),
                        static_cast<unsigned long long>(t.video->droppedQueueFull + t.video->droppedBySource),
                        t.video->encoder.c_str(), t.video->zeroCopy ? " zero-copy" : "");
        } else if (t.audio) {
            std::printf(" | %s %.1f dBFS drift %+.0f ppm%s", t.id.c_str(), t.audio->levels.maxPeakDb(),
                        t.audio->driftPpm, t.audio->active ? "" : " (silent)");
        }
    }
    std::printf(" | %.1f MB, %.1f MB/s, disk %s\n", static_cast<double>(s.totalBytes) / 1e6,
                s.writeBytesPerSecond / 1e6, std::string(toString(s.diskLevel)).c_str());
    std::fflush(stdout);
}

void printResult(const SessionResult& r) {
    std::printf("\nRecording %s (%s), duration %s, %zu pause(s)\n", std::string(toString(r.state)).c_str(),
                std::string(toString(r.reason)).c_str(), r.manifest.duration.toString().c_str(), r.manifest.pauses.size());
    for (const auto& t : r.manifest.tracks) {
        std::printf("  %-13s %-10s %-9s %s → %s  %s", t.id.c_str(), std::string(toString(t.state)).c_str(),
                    t.codec.c_str(), t.start ? t.start->toString().c_str() : "-", t.end ? t.end->toString().c_str() : "-",
                    t.file.c_str());
        if (t.mediaType == MediaType::Video) {
            std::printf("  %dx%d %s%s, %llu frames, %llu dropped", t.width, t.height, t.encoder.c_str(),
                        t.hardwareEncoder ? " (hw)" : "", static_cast<unsigned long long>(t.framesEncoded),
                        static_cast<unsigned long long>(t.framesDropped));
        } else {
            std::printf("  %d Hz x%d, drift %+.1f ppm, %llu hard corrections", t.sampleRate, t.channels, t.driftPpm,
                        static_cast<unsigned long long>(t.hardCorrections));
        }
        if (!t.error.empty()) std::printf("  ERROR: %s", t.error.c_str());
        std::printf("\n");
    }
}

int recover(const Options& o) {
    if (!o.recoverProject.empty()) {
        auto report = RecoveryService::recoverSession(o.recoverProject, o.recoverSession);
        if (!report) {
            std::fprintf(stderr, "recovery failed: %s\n", report.error().toString().c_str());
            return 1;
        }
        for (const auto& n : report->notes) std::printf("  %s\n", n.c_str());
        if (o.project) {
            if (auto p = services::importRecording(report->projectDir, report->manifest)) {
                std::printf("project: %s\n", (report->projectDir / "project.json").string().c_str());
            }
        }
        return report->tracksRecovered > 0 ? 0 : 1;
    }
    auto recovered = services::recoverInterruptedRecordings(
        o.registry.empty() ? ActiveSessionRegistry::defaultDirectory() : std::filesystem::path(o.registry));
    if (!recovered) {
        std::fprintf(stderr, "recovery failed: %s\n", recovered.error().toString().c_str());
        return 1;
    }
    if (recovered->empty()) std::printf("No interrupted recordings found.\n");
    for (const auto& r : *recovered) {
        std::printf("Recovered session %s in %s (%s)\n", r.report.sessionId.c_str(), r.projectDir.string().c_str(),
                    r.report.manifest.duration.toString().c_str());
        for (const auto& n : r.report.notes) std::printf("  %s\n", n.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) return 2;
    Logger::instance().addSink(makeStderrSink());
    Logger::instance().setLevel(o.logLevel);
    media::initializeFFmpeg(LogLevel::Warn);
    platform::initializePlatform();

    CaptureBackends backends = o.synthetic ? makeSyntheticBackends() : platform::createCaptureBackends();
    if (o.list) return listDevices(backends);
    if (o.recover || !o.recoverProject.empty()) return recover(o);
    if (!o.requestPermission.empty()) {
        const PermissionKind kind = o.requestPermission == "camera"       ? PermissionKind::Camera
                                    : o.requestPermission == "microphone" ? PermissionKind::Microphone
                                                                          : PermissionKind::ScreenCapture;
        std::atomic<int> result{-1};
        backends.permissions->request(kind, [&](PermissionStatus s) { result = static_cast<int>(s); });
        for (int i = 0; i < 600 && result < 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::printf("%s: %s\n", o.requestPermission.c_str(),
                    result < 0 ? "no answer" : std::string(toString(static_cast<PermissionStatus>(result.load()))).c_str());
        return result == static_cast<int>(PermissionStatus::Granted) ? 0 : 1;
    }

    // Output folder: --out, or a new project in the default projects directory.
    const std::string title = o.title.empty() ? "Recording " + json::utcNowIso8601().substr(0, 16) : o.title;
    std::filesystem::path projectDir;
    if (!o.out.empty()) {
        projectDir = o.out;
        if (auto st = fs::ensureDirectory(projectDir); !st) {
            std::fprintf(stderr, "%s\n", st.error().toString().c_str());
            return 1;
        }
    } else {
        auto created = project::ProjectStore::createProjectFolder(fs::defaultProjectsDirectory(), title);
        if (!created) {
            std::fprintf(stderr, "%s\n", created.error().toString().c_str());
            return 1;
        }
        projectDir = *created;
    }

    auto sources = services::openSources(backends, o.request);
    if (!sources) {
        std::fprintf(stderr, "cannot open sources: %s\n", sources.error().toString().c_str());
        return 1;
    }
    // Let devices deliver their first frames (screen content, camera warm-up).
    std::this_thread::sleep_for(std::chrono::milliseconds(sources->camera ? 800 : 300));

    auto session = RecordingSession::create(services::makeSessionConfig(*sources, o.request, projectDir, title));
    if (!session) {
        std::fprintf(stderr, "%s\n", session.error().toString().c_str());
        return 1;
    }
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    if (auto st = (*session)->start(); !st) {
        std::fprintf(stderr, "start failed: %s\n", st.error().toString().c_str());
        return 1;
    }
    std::printf("Recording into %s — press Ctrl+C to stop\n", projectDir.string().c_str());

    const auto start = std::chrono::steady_clock::now();
    auto lastStats = start;
    bool paused = false;
    bool resumed = false;
    while (!gStopRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (o.pauseAt >= 0 && !paused && t >= o.pauseAt) {
            (void)(*session)->pause();
            paused = true;
            std::printf("paused at %.1f s\n", t);
        }
        if (o.resumeAt >= 0 && paused && !resumed && t >= o.resumeAt) {
            (void)(*session)->resume();
            resumed = true;
            std::printf("resumed at %.1f s\n", t);
        }
        if (o.duration > 0 && t >= o.duration) break;
        const SessionState s = (*session)->state();
        if (s != SessionState::Recording && s != SessionState::Paused) break;  // auto-stopped (disk full, failures)
        if (o.stats && std::chrono::steady_clock::now() - lastStats >= std::chrono::seconds(1)) {
            lastStats = std::chrono::steady_clock::now();
            printStats((*session)->stats());
        }
    }
    (void)(*session)->stop();
    auto result = (*session)->waitForCompletion(std::chrono::seconds(60));
    sources->stopAll();
    if (!result) {
        std::fprintf(stderr, "recording did not finish: %s\n", result.error().toString().c_str());
        return 1;
    }
    printResult(*result);
    if (o.project && result->state == SessionState::Completed) {
        auto proj = services::importRecording(projectDir, result->manifest);
        if (proj) {
            std::printf("Project: %s\n", (projectDir / "project.json").string().c_str());
        } else {
            std::fprintf(stderr, "project creation failed: %s\n", proj.error().toString().c_str());
        }
    }
    Logger::instance().flush();
    return result->state == SessionState::Completed ? 0 : 1;
}
