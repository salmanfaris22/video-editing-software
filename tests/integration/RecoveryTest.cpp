// Crash-recovery tests: a real child process is SIGKILLed mid-recording and
// the recovery service must salvage its media (docs/RECORDING_ENGINE.md §8).

#include "capture/RecoveryService.h"
#include "core/FileSystem.h"
#include "media/FFmpeg.h"
#include "media/MediaProbe.h"
#include "project/ProjectStore.h"
#include "services/RecordingImporter.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#if !defined(_WIN32)
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

using namespace lectern;
using namespace lectern::capture;

#if !defined(_WIN32)
namespace {

struct KilledRecording {
    std::filesystem::path projectDir;
    std::filesystem::path registryDir;
    double recordedSeconds = 0;
};

// Starts the helper, lets it record for `seconds`, then SIGKILLs it.
KilledRecording recordAndKill(const test::TempDir& dir, double seconds) {
    KilledRecording k{dir / "project.lectern", dir / "registry", 0};
    std::filesystem::create_directories(k.projectDir);
    const std::string helper = LECTERN_CRASH_HELPER;
    const std::string project = k.projectDir.string();
    const std::string registry = k.registryDir.string();
    char* argv[] = {const_cast<char*>(helper.c_str()), const_cast<char*>(project.c_str()),
                    const_cast<char*>(registry.c_str()), nullptr};
    pid_t pid = 0;
    EXPECT_EQ(posix_spawn(&pid, helper.c_str(), nullptr, nullptr, argv, environ), 0);

    // Wait until the session is registered (recording has started).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool registered = false;
    while (!registered && std::chrono::steady_clock::now() < deadline) {
        std::error_code ec;
        registered = std::filesystem::exists(k.registryDir, ec) && !std::filesystem::is_empty(k.registryDir, ec);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(registered) << "helper never started recording";
    const auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    k.recordedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ::kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status));
    return k;
}

}  // namespace

TEST(Recovery, SalvagesRecordingAfterSigkill) {
    media::initializeFFmpeg(LogLevel::Error);
    test::TempDir dir("lectern-crash");
    const KilledRecording k = recordAndKill(dir, 3.0);

    RecoveryService service(k.registryDir);
    auto interrupted = service.findInterruptedSessions();
    ASSERT_TRUE(interrupted) << interrupted.error().toString();
    ASSERT_EQ(interrupted->size(), 1u);

    auto report = service.recover(interrupted->front());
    ASSERT_TRUE(report) << report.error().toString();
    EXPECT_EQ(report->tracksRecovered, 2) << ::testing::PrintToString(report->notes);
    EXPECT_EQ(report->tracksLost, 0);
    EXPECT_TRUE(report->manifest.recovered);
    // Loss is bounded by the cluster size (~1 s) plus buffering.
    EXPECT_GT(report->manifest.duration.toSecondsF(), k.recordedSeconds - 1.6);
    EXPECT_LE(report->manifest.duration.toSecondsF(), k.recordedSeconds + 0.5);
    for (const auto& t : report->manifest.tracks) {
        EXPECT_EQ(t.state, TrackState::Recovered) << t.id;
        auto info = media::probeMedia(k.projectDir / t.file);
        ASSERT_TRUE(info) << t.id << ": " << info.error().toString();
        EXPECT_GT(info->duration.toSecondsF(), 1.0);
        EXPECT_FALSE(std::filesystem::exists(k.projectDir / (t.file + ".partial")));
    }
    // Registry entry is gone; a second scan finds nothing.
    EXPECT_TRUE(service.findInterruptedSessions()->empty());

    // The recovered session imports like a normal recording.
    auto proj = services::importRecording(k.projectDir, report->manifest);
    ASSERT_TRUE(proj) << proj.error().toString();
    EXPECT_EQ(proj->timeline.tracks.size(), 2u);
    EXPECT_EQ(proj->recordings.front().state, "recovered");
}

TEST(Recovery, ReconstructsWhenManifestIsLost) {
    media::initializeFFmpeg(LogLevel::Error);
    test::TempDir dir("lectern-crash");
    const KilledRecording k = recordAndKill(dir, 2.0);
    RecoveryService service(k.registryDir);
    auto interrupted = service.findInterruptedSessions();
    ASSERT_TRUE(interrupted && interrupted->size() == 1u);
    std::filesystem::remove(interrupted->front().sessionDir / "session.json");

    auto report = RecoveryService::recoverSession(k.projectDir, interrupted->front().sessionDir);
    ASSERT_TRUE(report) << report.error().toString();
    EXPECT_EQ(report->tracksRecovered, 2);
    EXPECT_FALSE(report->notes.empty());
    bool sawScreen = false;
    for (const auto& t : report->manifest.tracks) sawScreen |= t.role == TrackRole::Screen;
    EXPECT_TRUE(sawScreen);
}
#endif

TEST(Recovery, NeverTouchesALiveSession) {
    test::TempDir dir;
    const auto sessionDir = dir / "project.lectern" / "recordings" / "live-session";
    std::filesystem::create_directories(sessionDir);
    ActiveSessionRegistry registry(dir / "registry");
    ASSERT_TRUE(registry.add({"live-session", dir / "project.lectern", sessionDir, "now"}));
    auto held = fs::FileLock::tryAcquire(sessionDir / "session.lock");  // simulates the recording process
    ASSERT_TRUE(held);

    RecoveryService service(dir / "registry");
    auto interrupted = service.findInterruptedSessions();
    ASSERT_TRUE(interrupted);
    EXPECT_TRUE(interrupted->empty());
    auto direct = RecoveryService::recoverSession(dir / "project.lectern", sessionDir);
    ASSERT_FALSE(direct);
    EXPECT_EQ(direct.error().code(), ErrorCode::InvalidState);
}
